用途：
    opencv接口封装，提供功能划分更贴近使用，更加便捷的接口函数

使用方法：
    1、编译开源代码，生成原始的opencv库文件，及外部头文件(后续引入项目使用时，确保编译环境一致，将库拷贝到项目中链接即可)
     运行脚本：
        chmod +x build_opencv.sh
        ./build_opencv.sh
     代码结构：
        opencv_output/
        ├── include/         # 头文件
        └── lib/             # 动态库（或 staticlib/ 静态库）
    2、使用脚本的输出，库文件全部使用lib中409结尾的库即可；若自行编译，可设置INSTALL_RPATH,确保输出库依赖项的正确查找
    3、导出使用时，确保封装api和opencv头文件的位置关系，能正确引用头文件。即可正常使用。