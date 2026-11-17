用途：
    无需安装 Axure，使用 Python 标准库解析 RP9 文件，生成静态 HTML 预览。
    不执行交互逻辑，不保证与 Axure 导出效果一致。需要 Python 3.10 以上。

使用：
    python axure_rp_viewer/rp_server.py [端口]
    python axure_rp_viewer/rp_viewer.py 文件.rp [-o 输出目录]
    python axure_rp_viewer/rp_gui.py [文件.rp]
    原生文件选择仅支持 Windows；网页及命令行可跨平台。
    PowerShell 设置 $env:RPVIEW_NO_OPEN='1' 禁止自动打开浏览器。

结构：
    rpformat.py：容器、二进制标签及对象引用解析
    rp_viewer.py：页面渲染、静态站点生成及命令行入口
    rp_server.py：本地上传服务及结果列表，仅监听 127.0.0.1
    rp_gui.py：Windows 文件选择入口
    tests/：标准库回归测试；review.md：功能评估与后续优化

输出：
    网页结果保存在本目录 rp_output/，每次上传独立保存，上传原件处理后删除。
    单次 HTTP 请求上限 64 MiB（包含表单开销）。结果需手动清理。
    命令行默认输出到原文件旁 *_view/；已有输出目录会覆盖同名文件，建议用新目录。
    index.html 为入口，pages.json 为页面信息，pages/、thumbs/、images/ 保存页面与图片。

验证：
    python -m unittest discover -s axure_rp_viewer/tests -v

来源：
    从 D:/Temp/AxureRP查看器 引入并修复，沿用当前仓库独立工具目录与中文说明风格。
