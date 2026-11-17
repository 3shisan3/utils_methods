# -*- coding: utf-8 -*-
"""rp_server.py — Axure RP 查看器本地网页界面。

用法:
    python rp_server.py [端口]

运行后自动打开浏览器 http://127.0.0.1:<端口>/ ，在网页里选择或拖入 .rp
文件即可解析并直接查看（仅监听本机回环 127.0.0.1，不上传到任何外部服务器）。
解析结果保存在脚本目录的 rp_output/ 下，可反复解析不同文件。
"""
import json
import tempfile
import shutil
from email.parser import BytesParser
from email.policy import default
import mimetypes
import os
import re
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, unquote, urlparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_ROOT = os.path.join(SCRIPT_DIR, 'rp_output')
UPLOAD_DIR = os.path.join(OUT_ROOT, '_uploads')
NO_OPEN = os.environ.get('RPVIEW_NO_OPEN') == '1'
NO_PAUSE = os.environ.get('RPVIEW_NO_PAUSE') == '1'

PAGE = """<!DOCTYPE html>
<html lang="zh"><head><meta charset="utf-8">
<title>Axure RP 原型查看器</title>
<style>
body{margin:0;background:#1f2429;color:#dfe3e6;font-family:'Microsoft YaHei',Arial,sans-serif}
.wrap{max-width:860px;margin:0 auto;padding:28px 20px 60px}
h1{font-size:22px;color:#fff;margin:0 0 6px}
.sub{color:#8b949c;font-size:13px;margin-bottom:22px}
#drop{border:2px dashed #4a5560;border-radius:10px;padding:36px;text-align:center;
      background:#262c33;cursor:pointer;transition:.15s}
#drop.on{border-color:#ffcf5c;background:#2d343c}
#drop b{color:#ffcf5c}
#pick{color:#58a6ff;cursor:pointer;text-decoration:underline;background:none;border:0;font-size:inherit;font-family:inherit}
#go{margin-top:16px;background:#2f81f7;color:#fff;border:0;border-radius:6px;padding:9px 26px;
    font-size:15px;cursor:pointer}
#go:disabled{opacity:.5;cursor:default}
#status{margin-top:14px;font-size:14px;min-height:20px}
#status.err{color:#ff7b72}
#status.ok{color:#56d364}
#result{margin-top:10px;display:none;background:#262c33;border-radius:8px;padding:16px}
#result a{color:#ffcf5c;font-size:16px;text-decoration:none;font-weight:bold}
#names{margin-top:8px;color:#8b949c;font-size:12px;line-height:1.8}
h2{font-size:15px;color:#aab4bd;margin:34px 0 8px}
.item{background:#262c33;border-radius:6px;padding:10px 14px;margin-bottom:8px;display:flex;justify-content:space-between}
.item a{color:#58a6ff;text-decoration:none;font-size:14px}
.item span{color:#8b949c;font-size:12px}
</style></head>
<body><div class="wrap">
<h1>Axure RP 原型查看器</h1>
<div class="sub">选择或拖入本地 .rp 文件，解析后在线查看（全程本机处理）</div>
<div id="drop">把 <b>.rp</b> 文件拖到这里，或 <button id="pick" type="button">点击选择文件</button>
<div style="margin-top:10px;font-size:12px;color:#8b949c" id="fname"></div>
<input type="file" id="file" accept=".rp" style="display:none">
</div>
<button id="go" disabled>开始解析</button>
<div id="status"></div>
<div id="result"></div>
<h2>已解析的文件</h2>
<div id="list">加载中…</div>
<script>
var file = null;
var drop = document.getElementById('drop'),
    inp  = document.getElementById('file'),
    go   = document.getElementById('go'),
    st   = document.getElementById('status'),
    res  = document.getElementById('result'),
    fname= document.getElementById('fname');
document.getElementById('pick').onclick = function(){ inp.click(); };
drop.onclick = function(e){ if(e.target.tagName !== 'BUTTON') inp.click(); };
['dragover','dragenter'].forEach(function(ev){
  drop.addEventListener(ev, function(e){ e.preventDefault(); drop.classList.add('on'); }); });
drop.addEventListener('dragleave', function(){ drop.classList.remove('on'); });
drop.addEventListener('drop', function(e){
  e.preventDefault(); drop.classList.remove('on');
  if(e.dataTransfer.files.length) setFile(e.dataTransfer.files[0]); });
inp.onchange = function(){ if(inp.files.length) setFile(inp.files[0]); };
function setFile(f){
  if(!/\\.rp$/i.test(f.name)){ setStatus('请选择 .rp 文件', true); return; }
  file = f; fname.textContent = '已选择：' + f.name + '（' + (f.size/1048576).toFixed(1) + ' MB）';
  go.disabled = false; setStatus('');
}
function setStatus(t, isErr){
  st.textContent = t || ''; st.className = isErr ? 'err' : (t ? 'ok' : '');
}
function esc(value){return String(value).replace(/[&<>"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];});}
function loadList(){
  fetch('/list').then(function(r){return r.json()}).then(function(j){
    var el = document.getElementById('list');
    if(!j.items.length){ el.textContent = '暂无'; return; }
    el.innerHTML = '';
    j.items.forEach(function(it){
      var d = document.createElement('div'); d.className = 'item';
      d.innerHTML = '<a href="' + it.url + '" target="_blank">' + esc(it.dir) + '</a>' +
                    '<span>' + it.pages + ' 页 · ' + new Date(it.mtime*1000).toLocaleString() + '</span>';
      el.appendChild(d);
    });
  });
}
go.onclick = function(){
  if(!file){ return; }
  go.disabled = true; setStatus('解析中，请稍候…'); res.style.display = 'none';
  var fd = new FormData(); fd.append('file', file, file.name);
  fetch('/parse', {method:'POST', body: fd})
    .then(function(r){ return r.json(); })
    .then(function(j){
      go.disabled = false;
      if(!j.ok){ setStatus('解析失败：' + j.error, true); return; }
      setStatus('解析完成：' + j.pages + ' 页');
      res.style.display = 'block';
      res.innerHTML = '<a href="' + j.url + '" target="_blank">打开查看器 →</a>' +
        '<div id="names">' + (j.warnings.length ? '警告: ' + j.warnings.map(esc).join('; ') + '<br>' : '') +
        '包含页面：' + j.names.join('、') + '</div>';
      loadList();
      window.open(j.url, '_blank');
    })
    .catch(function(e){ go.disabled = false; setStatus('请求失败：' + e, true); });
};
loadList();
</script></body></html>
"""


def sanitize(name):
    name = os.path.basename(name.replace('\\', '/'))
    return re.sub(r'[^\w.\-\u4e00-\u9fff]', '_', name).strip('._') or 'rpfile'


def run_parse(rp_path, out_dir):
    from rp_viewer import Viewer
    v = Viewer(rp_path, out_dir)
    v.load()
    v.write_site()
    return v


MAX_UPLOAD = 64 * 1024 * 1024


def parse_upload(content_type, body):
    """使用 MIME 解析器保留二进制载荷。"""
    message = BytesParser(policy=default).parsebytes(
        ('Content-Type: ' + content_type + '\r\nMIME-Version: 1.0\r\n\r\n').encode('ascii') + body)
    if message.get_content_type() != 'multipart/form-data' or not message.is_multipart():
        raise ValueError('需要 multipart/form-data 文件上传')
    files = [part for part in message.iter_parts()
             if part.get_param('name', header='content-disposition') == 'file'
             and part.get_filename()]
    if len(files) != 1:
        raise ValueError('请上传一个 .rp 文件')
    part = files[0]
    name = part.get_filename()
    blob = part.get_payload(decode=True)
    if not name.lower().endswith('.rp') or not blob:
        raise ValueError('请选择非空的 .rp 文件')
    return name, blob


def parse_bytes(fn, blob):
    """独立目录隔离任务，只发布成功结果，清理上传原件。"""
    if not fn.lower().endswith('.rp') or not blob or len(blob) > MAX_UPLOAD:
        return {'ok': False, 'error': '需要非空且不超过 64 MiB 的 .rp 文件'}
    os.makedirs(OUT_ROOT, exist_ok=True)
    work = tempfile.mkdtemp(prefix='_parse_', dir=OUT_ROOT)
    try:
        rp_path = os.path.join(work, 'input.rp')
        with open(rp_path, 'wb') as f:
            f.write(blob)
        out_dir = os.path.join(work, 'site')
        v = run_parse(rp_path, out_dir)
        with open(os.path.join(out_dir, 'pages.json'), encoding='utf-8') as f:
            pages = json.load(f)
        name = sanitize(os.path.splitext(fn)[0])[:80] + '_' + os.path.basename(work)[7:] + '_view'
        os.rename(out_dir, os.path.join(OUT_ROOT, name))
        return {'ok': True, 'url': '/view/%s/index.html' % quote(name),
                'pages': len(pages), 'names': [p['name'] for p in pages], 'warnings': v.warn}
    except Exception as e:
        return {'ok': False, 'error': '%s: %s' % (type(e).__name__, e)}
    finally:
        shutil.rmtree(work)


class Handler(BaseHTTPRequestHandler):
    server_version = 'RPViewer/1.0'

    def log_message(self, fmt, *args):
        pass

    def _send(self, code, body, ctype='text/html; charset=utf-8'):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ('/', '/index.html'):
            self._send(200, PAGE.encode('utf-8'))
        elif path == '/list':
            items = []
            if os.path.isdir(OUT_ROOT):
                for name in sorted(os.listdir(OUT_ROOT)):
                    if name.startswith('_'):
                        continue
                    pj = os.path.join(OUT_ROOT, name, 'pages.json')
                    if os.path.isfile(pj):
                        try:
                            meta = json.load(open(pj, encoding='utf-8'))
                            items.append({'dir': name, 'pages': len(meta),
                                          'url': '/view/%s/index.html' % quote(name),
                                          'mtime': int(os.path.getmtime(pj))})
                        except Exception:
                            pass
            items.sort(key=lambda x: -x['mtime'])
            self._send(200, json.dumps({'items': items}, ensure_ascii=False).encode('utf-8'),
                       'application/json; charset=utf-8')
        elif path.startswith('/view/'):
            rest = unquote(path[len('/view/'):])
            name, _, fn = rest.partition('/')
            name = sanitize(name)
            fn = (fn or 'index.html').replace('\\', '/')
            root = os.path.realpath(os.path.join(OUT_ROOT, name))
            full = os.path.realpath(os.path.join(root, fn))
            if name.startswith('_') or os.path.commonpath([os.path.realpath(OUT_ROOT), root, full]) != os.path.realpath(OUT_ROOT) or not full.startswith(root + os.sep) or not os.path.isfile(full):
                self._send(404, b'not found', 'text/plain')
                return
            ctype = mimetypes.guess_type(full)[0] or 'application/octet-stream'
            if full.endswith(('.html', '.json')):
                ctype += '; charset=utf-8'
            with open(full, 'rb') as f:
                self._send(200, f.read(), ctype)
        else:
            self._send(404, b'not found', 'text/plain')

    def do_POST(self):
        if urlparse(self.path).path != '/parse':
            self._send(404, b'not found', 'text/plain')
            return
        try:
            origin = self.headers.get('Origin')
            if origin and origin != 'http://127.0.0.1:%d' % self.server.server_address[1]:
                self._send(403, b'origin rejected', 'text/plain')
                return
            ln = int(self.headers.get('Content-Length', '0'))
            if not 0 < ln <= MAX_UPLOAD:
                self._send(413, b'upload exceeds 64 MiB or is empty', 'text/plain')
                return
            self.connection.settimeout(30)
            body = self.rfile.read(ln)
            if len(body) != ln:
                raise ValueError('上传数据不完整')
            fn, blob = parse_upload(self.headers.get('Content-Type', ''), body)
            resp = parse_bytes(fn or 'upload.rp', blob)
        except Exception as e:
            resp = {'ok': False, 'error': '%s: %s' % (type(e).__name__, e)}
        self._send(200, json.dumps(resp, ensure_ascii=False).encode('utf-8'),
                   'application/json; charset=utf-8')


def main():
    port = None
    for a in sys.argv[1:]:
        if a.isdigit():
            port = int(a)
    srv = None
    ports = [port] if port else list(range(8765, 8776))
    for p in ports:
        try:
            srv = ThreadingHTTPServer(('127.0.0.1', p), Handler)
            break
        except OSError:
            continue
    if srv is None:
        print('ERROR: no free port in %s' % ports)
        return 1
    url = 'http://127.0.0.1:%d/' % srv.server_address[1]
    print('RP viewer UI: %s  (Ctrl+C to stop)' % url)
    print('Output dir  : %s' % OUT_ROOT)
    if not NO_OPEN:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
