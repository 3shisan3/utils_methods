# 设置CMake最低版本要求  
cmake_minimum_required(VERSION 3.10)  
  
# 设置项目名称  
project(utils_test)

# 设置测试内容所在父目录
set(Utils_ROOTDIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# 添加编译输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)
  
# 添加源代码文件  
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_threadpool.cpp")  
  
# 添加头文件目录(测试不同头文件修改此处)  
include_directories(${Utils_ROOTDIR}
)

# 编译生成可执行程序
add_executable(${PROJECT_NAME} ${SOURCES})

# 添加需要链接的库文件  
# target_link_libraries(${PROJECT_NAME} 

# )

