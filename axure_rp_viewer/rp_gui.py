# -*- coding: utf-8 -*-
"""rp_gui.py — Axure RP 查看器（原生文件对话框版）。

用法:
    python rp_gui.py            # 弹出"选择文件"对话框，选中后自动解析并打开浏览器
    python rp_gui.py 文件.rp    # 直接解析（也支持把 .rp 拖到本脚本图标上）

解析结果生成 <rp文件名>_view/index.html 并自动用系统默认浏览器打开。
"""
import ctypes
import json
import os
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

NO_OPEN = os.environ.get('RPVIEW_NO_OPEN') == '1'
NO_PAUSE = os.environ.get('RPVIEW_NO_PAUSE') == '1'
LAST_FILE_MARK = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rp_gui_last.txt')

if os.name == 'nt':
    from ctypes import wintypes

    class OPENFILENAMEW(ctypes.Structure):
        _fields_ = [
            ('lStructSize', ctypes.c_ulong),
            ('hwndOwner', ctypes.c_void_p),
            ('hInstance', ctypes.c_void_p),
            ('lpstrFilter', ctypes.c_wchar_p),
            ('lpstrCustomFilter', ctypes.c_wchar_p),
            ('nMaxCustFilter', ctypes.c_ulong),
            ('nFilterIndex', ctypes.c_ulong),
            ('lpstrFile', ctypes.c_wchar_p),
            ('nMaxFile', ctypes.c_ulong),
            ('lpstrFileTitle', ctypes.c_wchar_p),
            ('nMaxFileTitle', ctypes.c_ulong),
            ('lpstrInitialDir', ctypes.c_wchar_p),
            ('lpstrTitle', ctypes.c_wchar_p),
            ('Flags', ctypes.c_ulong),
            ('nFileOffset', ctypes.c_ushort),
            ('nFileExtension', ctypes.c_ushort),
            ('lpstrDefExt', ctypes.c_wchar_p),
            ('lCustData', ctypes.c_void_p),
            ('lpfnHook', ctypes.c_void_p),
            ('lpTemplateName', ctypes.c_wchar_p),
            ('pvReserved', ctypes.c_void_p),
            ('dwReserved', ctypes.c_ulong),
            ('FlagsEx', ctypes.c_ulong),
        ]

    OFN_EXPLORER = 0x00080000
    OFN_PATHMUSTEXIST = 0x00000800
    OFN_FILEMUSTEXIST = 0x00001000
    OFN_HIDEREADONLY = 0x00000004


def pick_rp_file(initial_dir=None):
    """弹出 Windows 原生"打开文件"对话框（comdlg32），返回路径或 None。"""
    if os.name != 'nt':
        return None
    buf = ctypes.create_unicode_buffer(8192)
    filter_buf = 'Axure RP 文件 (*.rp)\0*.rp\0所有文件 (*.*)\0*.*\0'
    ofn = OPENFILENAMEW()
    ofn.lStructSize = ctypes.sizeof(OPENFILENAMEW)
    ofn.hwndOwner = None
    ofn.lpstrFilter = filter_buf
    ofn.lpstrFile = ctypes.cast(buf, ctypes.c_wchar_p)
    ofn.nMaxFile = len(buf)
    ofn.lpstrInitialDir = initial_dir or None
    ofn.lpstrTitle = '选择 Axure RP 文件'
    ofn.Flags = (OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY)
    ok = ctypes.windll.comdlg32.GetOpenFileNameW(ctypes.byref(ofn))
    return buf.value if ok else None


def load_last_dir():
    try:
        with open(LAST_FILE_MARK, encoding='utf-8') as f:
            d = f.read().strip()
            return d if os.path.isdir(d) else None
    except Exception:
        return None


def save_last_dir(path):
    try:
        with open(LAST_FILE_MARK, 'w', encoding='utf-8') as f:
            f.write(os.path.dirname(os.path.abspath(path)))
    except Exception:
        pass


def open_in_browser(path):
    if os.name == 'nt':
        os.startfile(path)  # noqa
    else:
        import webbrowser
        from pathlib import Path
        webbrowser.open(Path(path).resolve().as_uri())


def parse_and_open(rp_path, do_open=True):
    from rp_viewer import Viewer
    out_dir = os.path.splitext(rp_path)[0] + '_view'
    print('Parsing: %s' % rp_path)
    print('Output : %s' % out_dir)
    v = Viewer(rp_path, out_dir)
    v.load()
    v.write_site()
    index = os.path.join(out_dir, 'index.html')
    try:
        pages = json.load(open(os.path.join(out_dir, 'pages.json'), encoding='utf-8'))
        print('Pages  : %d' % len(pages))
        for p in pages:
            print('  - %s' % p['name'])
    except Exception:
        pass
    for w in v.warn:
        print('WARNING: %s' % w)
    print('Done.')
    if do_open and os.path.isfile(index):
        open_in_browser(index)
        print('Opened in browser.')
    return index


def main():
    rp_path = None
    for a in sys.argv[1:]:
        if not a.startswith('-') and os.path.isfile(a):
            rp_path = a
            break
    if rp_path is None:
        print('Select an Axure RP file...')
        rp_path = pick_rp_file(load_last_dir())
    if not rp_path:
        print('No file selected.')
        _pause()
        return 1
    save_last_dir(rp_path)
    try:
        parse_and_open(rp_path, do_open=not NO_OPEN)
        return 0
    except Exception as e:
        print('ERROR: %s' % e)
        traceback.print_exc()
        _pause()
        return 1


def _pause():
    if NO_PAUSE:
        return
    try:
        if sys.stdin and sys.stdin.isatty():
            input('Press Enter to exit...')
    except Exception:
        pass


if __name__ == '__main__':
    sys.exit(main())
