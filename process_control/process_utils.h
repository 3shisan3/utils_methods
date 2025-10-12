/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT
File:        process_utils.h
Version:     1.1
Author:      cjx
start date: 2025-10-10
Description: 进程控制方法（子进程启动）
    提供启动、停止、监控进程的功能（支持Windows和Linux平台）
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2025-10-10      cjx           create
2             2025-10-12      cjx           增强错误处理

*****************************************************************/

#ifndef PROCESS_UTILS_H
#define PROCESS_UTILS_H

#include <map>
#include <string>
#include <vector>

class ProcessUtils
{
public:
    /**
     * @brief 启动进程
     * @param processPath 进程路径（必须提供有效路径）
     * @param arguments 启动参数
     * @param workingDir 工作目录（为空时使用进程所在目录）
     * @param processName 进程名称（为空时从processPath提取）
     * @return 成功返回true，失败返回false
     */
    static bool startProcess(const std::string &processPath,
                             const std::vector<std::string> &arguments = {},
                             const std::string &workingDir = "",
                             const std::string &processName = "");

    /**
     * @brief 停止进程
     * @param processName 进程名称
     * @param force 是否强制终止
     * @return 成功返回true，失败返回false
     */
    static bool stopProcess(const std::string &processName, bool force = false);

    /**
     * @brief 检查进程是否正在运行
     * @param processName 进程名称
     * @return 正在运行返回true，否则返回false
     */
    static bool isProcessRunning(const std::string &processName);

    /**
     * @brief 获取进程ID
     * @param processName 进程名称
     * @return 进程ID，未找到返回-1
     */
    static int getProcessId(const std::string &processName);

    /**
     * @brief 获取所有匹配进程名的进程ID
     * @param processName 进程名称
     * @return 进程ID列表
     */
    static std::vector<int> getProcessIds(const std::string &processName);

    /**
     * @brief 读取PID文件
     * @param pidFile PID文件路径
     * @return 进程ID，读取失败返回-1
     */
    static int readPidFile(const std::string &pidFile);

    /**
     * @brief 写入PID文件
     * @param pidFile PID文件路径
     * @param pid 进程ID
     * @return 成功返回true，失败返回false
     */
    static bool writePidFile(const std::string &pidFile, int pid);

    /**
     * @brief 删除PID文件
     * @param pidFile PID文件路径
     * @return 成功返回true，失败返回false
     */
    static bool removePidFile(const std::string &pidFile);

    /**
     * @brief 通过PID停止进程
     * @param pid 进程ID
     * @param force 是否强制终止
     * @return 成功返回true，失败返回false
     */
    static bool stopProcessByPid(int pid, bool force = false);

    /* 通过进程名生成默认的相关信息 */

    /**
     * @brief 获取进程的PID文件路径
     * @param processName 进程名称（不能为空）
     * @return PID文件路径
     */
    static std::string getPidFile(const std::string &processName);

    /**
     * @brief 获取进程的可执行文件路径
     * @param processName 进程名称（不能为空）
     * @return 可执行文件路径
     */
    static std::string getExecutablePath(const std::string &processName);

    /**
     * @brief 获取进程的配置文件路径
     * @param processName 进程名称（不能为空）
     * @return 配置文件路径
     */
    static std::string getConfigPath(const std::string &processName);

private:
#ifdef _WIN32
    /**
     * @brief 在Windows上查找进程
     * @param processName 进程名称
     * @return 进程信息映射表
     */
    static std::map<int, std::string> findWindowsProcesses(const std::string &processName);
#endif

    /**
     * @brief 检查路径是否存在并打印提示信息
     * @param path 要检查的路径
     * @param pathType 路径类型描述（如"PID文件"、"配置文件"等）
     * @return 路径存在返回true，否则返回false
     */
    static bool checkPathExists(const std::string &path, const std::string &pathType);

    /**
     * @brief 验证进程名称是否有效
     * @param processName 进程名称
     * @return 有效返回true，否则返回false
     */
    static bool validateProcessName(const std::string &processName);

    /**
     * @brief 从文件路径中提取进程名称（不含路径和扩展名）
     * @param filePath 文件路径
     * @return 进程名称，提取失败返回空字符串
     */
    static std::string extractProcessNameFromPath(const std::string &filePath);

#ifndef _WIN32
    /**
     * @brief Linux专用：准备execvp参数
     * @param processPath 进程路径
     * @param arguments 参数列表
     * @return 参数数组（需要调用者释放内存）
     */
    static std::vector<char*> prepareExecArgs(const std::string& processPath, 
                                             const std::vector<std::string>& arguments);

    /**
     * @brief Linux专用：清理execvp参数内存
     * @param args 参数数组
     */
    static void cleanupExecArgs(std::vector<char*>& args);
#endif
};
#endif // PROCESS_UTILS_H