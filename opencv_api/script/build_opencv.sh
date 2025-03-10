#!/bin/bash
set -e  # 脚本执行出错则退出

# 配置参数（根据需求修改）
OPENCV_VERSION="4.9.0"        # OpenCV 版本
BUILD_TYPE="shared"           # 编译类型：shared（动态库）或 static（静态库）
TARGET_DIR="./opencv_output"  # 导出目标目录
BUILD_DIR="./build"           # 编译临时目录
INSTALL_DIR="./install"       # 安装中间目录
NUM_JOBS=8                    # 编译线程数

# 获取脚本的绝对路径，确保路径正确
SCRIPT_DIR=$(pwd)

# 清理旧文件
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$TARGET_DIR" 2>/dev/null

# 安装依赖项（以 Ubuntu 为例）
sudo apt-get update
sudo apt-get install -y \
    cmake g++ git wget unzip \
    libgtk2.0-dev pkg-config libavcodec-dev libavformat-dev libswscale-dev \
    libtbb2 libtbb-dev libjpeg-dev libpng-dev libwebp-dev libopenjp2-7-dev

# 下载 OpenCV 源码
OPENCV_ZIP="opencv-${OPENCV_VERSION}.zip"
wget -O "$OPENCV_ZIP" "https://github.com/opencv/opencv/archive/${OPENCV_VERSION}.zip"
unzip "$OPENCV_ZIP" -d .
mv "opencv-${OPENCV_VERSION}" opencv

# 配置 CMake 参数
CMAKE_OPTIONS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$SCRIPT_DIR/$INSTALL_DIR"  # 使用绝对路径
    -DBUILD_LIST=core,imgproc,imgcodecs  # 按需添加模块（减少编译时间）
    -DBUILD_EXAMPLES=OFF
    -DBUILD_TESTS=OFF

    # 设置 RPATH 相关参数
    -DCMAKE_INSTALL_RPATH="$ORIGIN"          # 使用相对路径 `$ORIGIN`（Linux）或 `@loader_path`（macOS）
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON   # 自动添加链接路径到 RPATH
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON      # 构建时也使用安装后的 RPATH
)

# 动态库或静态库
if [[ "$BUILD_TYPE" == "shared" ]]; then
    CMAKE_OPTIONS+=(-DBUILD_SHARED_LIBS=ON)
else
    CMAKE_OPTIONS+=(-DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON)
fi

# 创建编译目录
mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"

# 执行 CMake 和编译
cmake "${CMAKE_OPTIONS[@]}" ../opencv
make -j"$NUM_JOBS"
make install

# 整理输出文件到目标目录（使用绝对路径）
mkdir -p "$SCRIPT_DIR/$TARGET_DIR"
cp -r "$SCRIPT_DIR/$INSTALL_DIR/include/opencv4" "$SCRIPT_DIR/$TARGET_DIR/include"

if [[ "$BUILD_TYPE" == "shared" ]]; then
    mkdir -p "$SCRIPT_DIR/$TARGET_DIR/lib"
    find "$SCRIPT_DIR/$INSTALL_DIR/lib" -name 'libopencv_*.so*' -exec cp {} "$SCRIPT_DIR/$TARGET_DIR/lib" \;
else
    mkdir -p "$SCRIPT_DIR/$TARGET_DIR/staticlib"
    find "$SCRIPT_DIR/$INSTALL_DIR/lib" -name 'libopencv_*.a' -exec cp {} "$SCRIPT_DIR/$TARGET_DIR/staticlib" \;
fi

# 清理临时文件（可选）
cd ..
rm -rf "$BUILD_DIR" "$INSTALL_DIR" opencv "$OPENCV_ZIP"

echo "编译完成！结果已导出至：$TARGET_DIR"