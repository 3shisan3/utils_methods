#include "process_utils.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

// Windows头文件必须在最前面，并且按照正确顺序包含
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#else
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

bool ProcessUtils::startProcess(const std::string &processPath,
                                const std::vector<std::string> &arguments,
                                const std::string &workingDir,
                                const std::string &processName)
{
    // 验证进程路径不能为空
    if (processPath.empty())
    {
        std::cerr << "错误: 进程路径不能为空" << std::endl;
        return false;
    }

    std::string actualProcessName = processName;

    // 如果进程名称为空，从进程路径中提取
    if (processName.empty() && !processPath.empty())
    {
        actualProcessName = extractProcessNameFromPath(processPath);
        if (actualProcessName.empty())
        {
            std::cerr << "错误: 无法从路径中提取进程名称: " << processPath << std::endl;
            return false;
        }
        std::cout << "从路径提取进程名称: " << actualProcessName << std::endl;
    }

    // 验证进程名称
    if (!validateProcessName(actualProcessName))
    {
        std::cerr << "错误: 进程名称无效: " << actualProcessName << std::endl;
        return false;
    }

    // 检查可执行文件是否存在
    if (!checkPathExists(processPath, "可执行文件"))
    {
        std::cerr << "错误: 可执行文件不存在: " << processPath << std::endl;
        std::cout << "提示: 请确保可执行文件存在或提供完整路径" << std::endl;
        return false;
    }

#ifdef _WIN32
    // 构建命令行参数
    std::string commandLine = "\"" + processPath + "\"";
    for (const auto &arg : arguments)
    {
        commandLine += " \"" + arg + "\"";
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::string actualWorkingDir = workingDir.empty() ? "" : workingDir;
    if (actualWorkingDir.empty())
    {
        // 使用可执行文件所在目录作为工作目录
        size_t lastSlash = processPath.find_last_of("\\/");
        if (lastSlash != std::string::npos)
        {
            actualWorkingDir = processPath.substr(0, lastSlash);
        }
    }

    std::cout << "启动进程: " << actualProcessName << std::endl;
    std::cout << "可执行文件: " << processPath << std::endl;
    std::cout << "命令行: " << commandLine << std::endl;
    if (!actualWorkingDir.empty())
    {
        std::cout << "工作目录: " << actualWorkingDir << std::endl;
    }

    // 创建进程
    if (!CreateProcessA(
            processPath.c_str(),
            const_cast<LPSTR>(commandLine.c_str()),
            NULL,
            NULL,
            FALSE,
            CREATE_NEW_CONSOLE,
            NULL,
            actualWorkingDir.empty() ? NULL : actualWorkingDir.c_str(),
            &si,
            &pi))
    {
        DWORD error = GetLastError();
        std::cerr << "CreateProcess失败，错误代码: " << error << std::endl;

        // 提供常见错误代码的解释
        if (error == ERROR_FILE_NOT_FOUND)
        {
            std::cerr << "错误详情: 系统找不到指定的文件" << std::endl;
        }
        else if (error == ERROR_ACCESS_DENIED)
        {
            std::cerr << "错误详情: 拒绝访问" << std::endl;
        }

        return false;
    }

    // 关闭进程和线程句柄（重要：避免句柄泄漏）
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::cout << "成功启动进程: " << actualProcessName << " (PID: " << pi.dwProcessId << ")" << std::endl;

    // 写入PID文件
    std::string pidFile = getPidFile(actualProcessName);
    if (writePidFile(pidFile, pi.dwProcessId))
    {
        std::cout << "已创建PID文件: " << pidFile << std::endl;
    }

    return true;
#else
    // Linux实现 - 修复了参数处理的内存安全问题
    pid_t pid = fork();
    if (pid == 0)
    {
        // 子进程
        std::string actualWorkingDir = workingDir;
        if (actualWorkingDir.empty())
        {
            // 使用可执行文件所在目录作为工作目录
            size_t lastSlash = processPath.find_last_of('/');
            if (lastSlash != std::string::npos)
            {
                actualWorkingDir = processPath.substr(0, lastSlash);
            }
        }

        if (!actualWorkingDir.empty())
        {
            if (chdir(actualWorkingDir.c_str()) != 0)
            {
                std::cerr << "切换工作目录失败: " << strerror(errno) << std::endl;
                exit(EXIT_FAILURE);
            }
        }

        // 准备exec参数（修复版本）
        auto args = prepareExecArgs(processPath, arguments);

        std::cout << "执行进程: " << processPath;
        for (size_t i = 1; args[i] != nullptr; ++i)
        {
            std::cout << " " << args[i];
        }
        std::cout << std::endl;

        // 使用execvp执行进程
        execvp(args[0], args.data());

        // 如果execvp返回，说明执行失败
        std::cerr << "执行进程失败 '" << args[0] << "': " << strerror(errno) << std::endl;

        // 清理内存后退出
        cleanupExecArgs(args);
        exit(EXIT_FAILURE);
    }
    else if (pid > 0)
    {
        // 父进程 - 忽略SIGCHLD信号，避免僵尸进程
        signal(SIGCHLD, SIG_IGN);
        std::cout << "成功启动进程 " << actualProcessName << " (PID: " << pid << ")" << std::endl;

        // 写入PID文件
        std::string pidFile = getPidFile(actualProcessName);
        if (writePidFile(pidFile, pid))
        {
            std::cout << "已创建PID文件: " << pidFile << std::endl;
        }

        return true;
    }
    else
    {
        // fork失败
        std::cerr << "Fork失败: " << strerror(errno) << std::endl;
        return false;
    }
#endif
}

bool ProcessUtils::stopProcess(const std::string &processName, bool force)
{
    if (processName.empty())
    {
        std::cerr << "错误: 进程名称不能为空" << std::endl;
        return false;
    }

    if (!validateProcessName(processName))
    {
        std::cerr << "错误: 进程名称无效: " << processName << std::endl;
        return false;
    }

    std::cout << "正在停止进程: " << processName << (force ? " (强制模式)" : " (优雅模式)") << std::endl;

    // 首先尝试通过进程名查找
    int pid = getProcessId(processName);
    if (pid == -1)
    {
        // 如果通过进程名没找到，尝试通过PID文件查找
        std::string pidFile = getPidFile(processName);
        pid = readPidFile(pidFile);
        if (pid != -1)
        {
            std::cout << "通过PID文件找到进程ID: " << pid << std::endl;

            // 验证PID对应的进程是否确实存在且名称匹配
            if (!isProcessRunning(processName))
            {
                std::cout << "警告: PID文件中的进程已不存在，删除无效PID文件" << std::endl;
                removePidFile(pidFile);
                return false;
            }
        }
        else
        {
            std::cout << "警告: PID文件不存在或无效: " << pidFile << std::endl;
        }
    }

    if (pid != -1)
    {
        bool result = stopProcessByPid(pid, force);
        if (result)
        {
            // 停止成功后删除PID文件
            std::string pidFile = getPidFile(processName);
            if (removePidFile(pidFile))
            {
                std::cout << "已删除PID文件: " << pidFile << std::endl;
            }
        }
        return result;
    }
    else
    {
        std::cerr << "错误: 未找到运行的进程: " << processName << std::endl;
        return false;
    }
}

bool ProcessUtils::isProcessRunning(const std::string &processName)
{
    if (processName.empty())
    {
        std::cerr << "错误: 进程名称不能为空" << std::endl;
        return false;
    }

    bool running = getProcessId(processName) != -1;
    std::cout << "进程 " << processName << " 运行状态: " << (running ? "运行中" : "未运行") << std::endl;
    return running;
}

int ProcessUtils::getProcessId(const std::string &processName)
{
    if (processName.empty())
    {
        std::cerr << "错误: 进程名称不能为空" << std::endl;
        return -1;
    }

    auto pids = getProcessIds(processName);
    return pids.empty() ? -1 : pids[0];
}

std::vector<int> ProcessUtils::getProcessIds(const std::string &processName)
{
    std::vector<int> pids;

    if (processName.empty())
    {
        std::cerr << "错误: 进程名称不能为空" << std::endl;
        return pids;
    }

#ifdef _WIN32
    auto processes = findWindowsProcesses(processName);
    for (const auto &proc : processes)
    {
        pids.push_back(proc.first);
    }
#else
    DIR *dir = opendir("/proc");
    if (!dir)
    {
        std::cerr << "无法打开/proc目录: " << strerror(errno) << std::endl;
        return pids;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (entry->d_type == DT_DIR && isdigit(entry->d_name[0]))
        {
            int pid = atoi(entry->d_name);

            // 检查多个来源以提高准确性
            std::string exePath = std::string("/proc/") + entry->d_name + "/exe";
            std::string cmdPath = std::string("/proc/") + entry->d_name + "/cmdline";

            char linkTarget[1024];
            ssize_t len = readlink(exePath.c_str(), linkTarget, sizeof(linkTarget) - 1);
            if (len != -1)
            {
                linkTarget[len] = '\0';
                std::string target(linkTarget);
                size_t lastSlash = target.find_last_of('/');
                if (lastSlash != std::string::npos)
                {
                    std::string exeName = target.substr(lastSlash + 1);
                    if (exeName == processName)
                    {
                        pids.push_back(pid);
                        continue;
                    }
                }
            }

            // 检查cmdline作为备选
            std::ifstream cmdFile(cmdPath);
            if (cmdFile)
            {
                std::string cmdLine;
                std::getline(cmdFile, cmdLine);

                if (!cmdLine.empty())
                {
                    size_t pos = cmdLine.find('\0');
                    std::string exeName = cmdLine.substr(0, pos);

                    size_t lastSlash = exeName.find_last_of('/');
                    if (lastSlash != std::string::npos)
                    {
                        exeName = exeName.substr(lastSlash + 1);
                    }

                    if (exeName == processName)
                    {
                        pids.push_back(pid);
                    }
                }
            }
        }
    }

    closedir(dir);
#endif

    std::cout << "找到 " << pids.size() << " 个 " << processName << " 进程实例" << std::endl;
    return pids;
}

int ProcessUtils::readPidFile(const std::string &pidFile)
{
    if (pidFile.empty())
    {
        std::cerr << "错误: PID文件路径不能为空" << std::endl;
        return -1;
    }

    if (!checkPathExists(pidFile, "PID文件"))
    {
        return -1;
    }

    std::ifstream file(pidFile);
    if (!file.is_open())
    {
        std::cerr << "无法打开PID文件: " << pidFile << std::endl;
        return -1;
    }

    int pid;
    file >> pid;
    file.close();

    if (pid <= 0)
    {
        std::cerr << "PID文件内容无效: " << pidFile << std::endl;
        return -1;
    }

    std::cout << "从PID文件读取到进程ID: " << pid << " (" << pidFile << ")" << std::endl;
    return pid;
}

bool ProcessUtils::writePidFile(const std::string &pidFile, int pid)
{
    if (pidFile.empty())
    {
        std::cerr << "错误: PID文件路径不能为空" << std::endl;
        return false;
    }

    if (pid <= 0)
    {
        std::cerr << "错误: 无效的进程ID: " << pid << std::endl;
        return false;
    }

    std::ofstream file(pidFile);
    if (!file.is_open())
    {
        std::cerr << "无法创建PID文件: " << pidFile << std::endl;
        return false;
    }

    file << pid;
    file.close();

    std::cout << "已写入PID文件: " << pidFile << " (PID: " << pid << ")" << std::endl;
    return true;
}

bool ProcessUtils::removePidFile(const std::string &pidFile)
{
    if (pidFile.empty())
    {
        std::cerr << "错误: PID文件路径不能为空" << std::endl;
        return false;
    }

    if (std::remove(pidFile.c_str()) == 0)
    {
        std::cout << "已删除PID文件: " << pidFile << std::endl;
        return true;
    }
    else
    {
        // 文件不存在不算错误
        if (errno == ENOENT)
        {
            std::cout << "PID文件不存在，无需删除: " << pidFile << std::endl;
            return true;
        }
        std::cerr << "删除PID文件失败: " << pidFile << " - " << strerror(errno) << std::endl;
        return false;
    }
}

bool ProcessUtils::stopProcessByPid(int pid, bool force)
{
    if (pid <= 0)
    {
        std::cerr << "错误: 无效的进程ID: " << pid << std::endl;
        return false;
    }

#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess == NULL)
    {
        DWORD error = GetLastError();
        std::cerr << "无法打开进程句柄 (PID: " << pid << "), 错误: " << error << std::endl;
        return false;
    }

    // 使用RAII管理句柄
    struct HandleGuard
    {
        HANDLE handle;
        HandleGuard(HANDLE h) : handle(h) {}
        ~HandleGuard()
        {
            if (handle)
                CloseHandle(handle);
        }
    } guard(hProcess);

    BOOL result = TerminateProcess(hProcess, force ? 1 : 0);

    if (result)
    {
        std::cout << "成功停止进程 (PID: " << pid << ")" << std::endl;
    }
    else
    {
        std::cerr << "停止进程失败 (PID: " << pid << ")" << std::endl;
    }

    return result != FALSE;
#else
    int signal = force ? SIGKILL : SIGTERM;
    std::cout << "向进程发送信号 (PID: " << pid << ", 信号: " << signal << ")" << std::endl;

    if (kill(pid, signal) == 0)
    {
        if (!force)
        {
            // 优雅终止，等待进程结束（最多5秒）
            for (int i = 0; i < 50; ++i)
            {
                if (kill(pid, 0) != 0)
                {
                    std::cout << "进程已优雅终止 (PID: " << pid << ")" << std::endl;
                    return true;
                }
                usleep(100000); // 100ms
            }
            // 优雅终止超时，转为强制终止
            std::cout << "优雅终止超时，转为强制终止 (PID: " << pid << ")" << std::endl;
            return kill(pid, SIGKILL) == 0;
        }
        std::cout << "进程已强制终止 (PID: " << pid << ")" << std::endl;
        return true;
    }
    else
    {
        std::cerr << "发送信号失败 (PID: " << pid << "): " << strerror(errno) << std::endl;
        return false;
    }
#endif
}

std::string ProcessUtils::getPidFile(const std::string &processName)
{
    if (processName.empty())
    {
        std::cerr << "错误: 进程名称不能为空" << std::endl;
        return "";
    }

    if (!validateProcessName(processName))
    {
        std::cerr << "错误: 进程名称无效: " << processName << std::endl;
        return "";
    }

    std::string pidFile;

#ifdef _WIN32
    pidFile = processName + ".pid";
#else
    // 尝试系统PID目录
    std::string systemPidDir = "/var/run";
    struct stat info;
    if (stat(systemPidDir.c_str(), &info) == 0 && (info.st_mode & S_IFDIR))
    {
        if (access(systemPidDir.c_str(), W_OK) == 0)
        {
            pidFile = systemPidDir + "/" + processName + ".pid";
        }
        else
        {
            std::cout << "警告: 无权限写入 " << systemPidDir << "，使用临时目录" << std::endl;
            pidFile = "/tmp/" + processName + ".pid";
        }
    }
    else
    {
        pidFile = "/tmp/" + processName + ".pid";
    }
#endif

    return pidFile;
}

std::string ProcessUtils::getExecutablePath(const std::string &processName)
{
    if (processName.empty())
    {
        std::cerr << "错误: 进程名称不能为空" << std::endl;
        return "";
    }

    if (!validateProcessName(processName))
    {
        std::cerr << "错误: 进程名称无效: " << processName << std::endl;
        return "";
    }

    std::string execPath;

#ifdef _WIN32
    execPath = processName + ".exe";
#else
    execPath = "./" + processName; // 相对路径，依赖调用者确保路径正确
#endif

    return execPath;
}

std::string ProcessUtils::getConfigPath(const std::string &processName)
{
    if (processName.empty())
    {
        std::cerr << "错误: 进程名称不能为空" << std::endl;
        return "";
    }

    if (!validateProcessName(processName))
    {
        std::cerr << "错误: 进程名称无效: " << processName << std::endl;
        return "";
    }

    return processName + "_config.yaml";
}

bool ProcessUtils::checkPathExists(const std::string &path, const std::string &pathType)
{
    if (path.empty())
    {
        return false;
    }

#ifdef _WIN32
    DWORD attrib = GetFileAttributesA(path.c_str());
    bool exists = (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat info;
    bool exists = (stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode));
#endif

    if (exists)
    {
        std::cout << pathType << "存在: " << path << std::endl;
    }
    else
    {
        std::cout << "提示: " << pathType << "不存在: " << path << std::endl;
    }

    return exists;
}

bool ProcessUtils::validateProcessName(const std::string &processName)
{
    if (processName.empty())
    {
        return false;
    }

    // 检查是否包含非法字符
    const std::string illegalChars = "\\/:*?\"<>|";
    for (char c : processName)
    {
        if (illegalChars.find(c) != std::string::npos)
        {
            return false;
        }
    }

    // 检查是否以点开头
    if (processName[0] == '.')
    {
        return false;
    }

    return true;
}

std::string ProcessUtils::extractProcessNameFromPath(const std::string &filePath)
{
    if (filePath.empty())
    {
        return "";
    }

    // 查找最后一个路径分隔符
    size_t lastSlash = filePath.find_last_of("/\\");
    std::string fileName = (lastSlash == std::string::npos) ? filePath : filePath.substr(lastSlash + 1);

    if (fileName.empty())
    {
        return "";
    }

    // 去除文件扩展名
    size_t lastDot = fileName.find_last_of('.');
    if (lastDot != std::string::npos)
    {
        fileName = fileName.substr(0, lastDot);
    }

    // 如果去除扩展名后为空，使用原始文件名
    if (fileName.empty())
    {
        fileName = (lastSlash == std::string::npos) ? filePath : filePath.substr(lastSlash + 1);
    }

    return fileName;
}

#ifdef _WIN32

// 宽字符串转ANSI字符串的辅助函数
static std::string wstringToString(const std::wstring &wstr)
{
    if (wstr.empty())
        return "";

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0)
        return "";

    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size_needed, nullptr, nullptr);
    return str;
}

std::map<int, std::string> ProcessUtils::findWindowsProcesses(const std::string &processName)
{
    std::map<int, std::string> processes;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
    {
        std::cerr << "创建进程快照失败，错误代码: " << GetLastError() << std::endl;
        return processes;
    }

    // 使用RAII管理句柄
    struct SnapGuard
    {
        HANDLE handle;
        SnapGuard(HANDLE h) : handle(h) {}
        ~SnapGuard()
        {
            if (handle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle);
            }
        }
    } guard(hSnapshot);

    PROCESSENTRY32W pe; // 明确使用宽字符版本
    pe.dwSize = sizeof(PROCESSENTRY32W);

    // 使用宽字符版本的Process32FirstW
    if (Process32FirstW(hSnapshot, &pe))
    {
        do
        {
            // 将宽字符转换为ANSI字符串
            std::wstring wstrExeFile(pe.szExeFile);
            std::string currentProcess = wstringToString(wstrExeFile);

            std::string extractedName = extractProcessNameFromPath(currentProcess);

            // 使用精确匹配
            if (currentProcess == processName ||
                currentProcess == processName + ".exe" ||
                extractedName == processName)
            {
                processes[pe.th32ProcessID] = currentProcess;
            }
        } while (Process32NextW(hSnapshot, &pe));
    }
    else
    {
        std::cerr << "Process32FirstW失败，错误代码: " << GetLastError() << std::endl;
    }

    return processes;
}
#endif

// Linux专用函数实现
#ifndef _WIN32
std::vector<char *> ProcessUtils::prepareExecArgs(const std::string &processPath,
                                                  const std::vector<std::string> &arguments)
{
    std::vector<char *> args;

    // 关键修复：分配持久内存存储参数
    // 使用静态vector确保参数在execvp调用前保持有效
    static std::vector<std::string> argStorage;
    argStorage.clear();

    // 存储进程路径
    argStorage.push_back(processPath);
    args.push_back(const_cast<char *>(argStorage.back().c_str()));

    // 存储所有参数
    for (const auto &arg : arguments)
    {
        argStorage.push_back(arg);
        args.push_back(const_cast<char *>(argStorage.back().c_str()));
    }

    // 添加终止的nullptr
    args.push_back(nullptr);

    return args;
}

void ProcessUtils::cleanupExecArgs(std::vector<char *> &args)
{
    // 由于我们使用静态vector，这里只需要清空即可
    // 在实际使用中，参数会在execvp成功后自动被新进程映像替换
    // 或者在execvp失败后随进程退出而自动释放
    args.clear();
}
#endif