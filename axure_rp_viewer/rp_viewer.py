# -*- coding: utf-8 -*-
"""rp_viewer.py — Axure RP 9 (.rp) 文件解析与浏览器查看器。

用法:
    python rp_viewer.py <文件.rp> [-o 输出目录]

生成静态 HTML 站点: index.html (侧栏页面树 + 缩略图) + pages/*.html + images/*
纯 Python 3 标准库, 无需安装 Axure。

逆向所得格式要点 (RP 9):
  容器 = 15 字节头 + LZ4 式压缩的索引 JSON (每页 parts 的文件偏移) + 条目流
  条目 = [u32 flag][u32 len][gzip 或 jpeg/raw]
  页面 blob = gzip; 内部为标签图值格式, 尾部对象表 + GUID 池
  页面名: 页面包 GUID (blob 池[1]) <-> DesignDocument 池 <-> PackageInfo
"""
import argparse
import hashlib
import html
import json
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rpformat as rf

HASH_LOCATION = 0xC0000065   # {X, Y}
HASH_SIZE = 0xC0000069       # {Width, Height}
HASH_FILL = 0xC0000007       # {fill-type, fill-color}
HASH_IMAGE = 0xC000001B      # {Stream, Extension, Hash}


def u32_of(v):
    return v & 0xFFFFFFFF


def prop_map(style):
    out = {}
    if not isinstance(style, dict):
        return out
    for ent in style.get('PropList', []) or []:
        if isinstance(ent, dict) and 'DOAName' in ent:
            out[u32_of(ent['DOAName'])] = ent.get('Value')
    return out


def argb_css(v):
    try:
        v = int(v) & 0xFFFFFFFF
    except Exception:
        return None
    a = (v >> 24) & 0xFF
    r = (v >> 16) & 0xFF
    g = (v >> 8) & 0xFF
    b = v & 0xFF
    if a == 0:
        return 'rgb(%d,%d,%d)' % (r, g, b)
    return 'rgba(%d,%d,%d,%.3f)' % (r, g, b, a / 255.0)


def lz_decode_index(data):
    """解码容器头后的 LZ4 式索引 JSON。流从 data[13] 的 token 开始,
    字面量自 data[15] 起; dist==0 表示流结束。"""
    out = bytearray()
    pos = 13
    n = len(data)
    while pos + 2 <= n:
        tok = data[pos]; pos += 1
        L = tok >> 4
        M = tok & 0x0F
        if L == 15:
            while pos < n:
                b = data[pos]; pos += 1
                L += b
                if b != 255:
                    break
        out += data[pos:pos + L]
        pos += L
        if pos + 2 > n:
            break
        dist = struct.unpack_from('<H', data, pos)[0]
        pos += 2
        if dist == 0:
            break
        if M == 15:
            while pos < n:
                b = data[pos]; pos += 1
                M += b
                if b != 255:
                    break
        mlen = M + 4
        if dist > len(out):
            break
        start = len(out) - dist
        for i in range(mlen):
            out.append(out[start + i])
        if len(out) > 50 * 1024 * 1024:
            break
    return bytes(out)


class Viewer(object):
    def __init__(self, rp_path, out_dir):
        self.rp_path = rp_path
        self.out_dir = out_dir
        self.img_written = {}
        self.warn = []

    # ---------------- container ----------------
    def load(self):
        with open(self.rp_path, 'rb') as f:
            data = f.read()
        if data[:4] != b'\xac\xef\x09\x00':
            raise rf.ParseError('仅支持 Axure RP 9 容器')
        self.data = data
        ents = rf.walk_entries(data)
        start = rf.find_stream_start(data)
        self.rel = {}
        self.pages = []            # [(rel_hdr, decompressed)]
        self.thumbs = {}           # rel_hdr -> jpeg bytes
        self.dd = None
        for eidx, flag, ln, off, kind in ents:
            hdr = off - 8
            rel = hdr - start
            self.rel[rel] = (eidx, kind, off, ln)
            if kind == 'gzip':
                dec = zlib.decompress(data[off:off + ln], 31)
                t = rf.blob_type_name(dec)
                if t == 'Axure:Page':
                    self.pages.append((rel, dec))
                elif t == 'Axure:DesignDocument':
                    self.dd = rf.decode_blob(dec, 'DD')
            elif kind == 'jpeg':
                self.thumbs[rel] = data[off:off + ln]
        self.index = None
        try:
            self.index = json.loads(lz_decode_index(data).decode('utf-8'))
        except Exception as e:
            self.warn.append('索引解码失败 (%s), 回退到顺序配对' % e)
        if not self.pages:
            raise SystemExit('未找到页面数据 (Axure:Page)')

    # ---------------- sitemap / names ----------------
    def build_outline(self):
        """返回 (items, page_by_guid):
        items = [{'depth','name','n','guid','folder'}]
        page_by_guid: 包 GUID -> 页面 blob 的 rel 偏移。
        """
        objs = self.dd['objects'] if self.dd else {}
        pool = [rf.format_guid(g) for g in (self.dd['pool'] if self.dd else [])]
        # PackageInfo: guid -> name
        guid2name = {}
        page_guids = []
        if self.dd:
            for ref in self.dd['root'].get('PackageInfos', []) or []:
                oid = ref.get('__ref__') if isinstance(ref, dict) else None
                pkg = objs.get(oid)
                if not isinstance(pkg, dict) or pkg.get('__type__') != 'Axure:PackageInfo':
                    continue
                pid = pkg.get('package-id')
                nr = pid.get('__named_ref__') if isinstance(pid, dict) else None
                if not nr:
                    continue
                n = nr[1]
                # named_ref 的第二载荷是 1-based 的 GUID 池序号 (经验证)
                guid = pool[n - 1] if isinstance(n, int) and 0 < n <= len(pool) else None
                if guid:
                    guid2name[guid] = pkg.get('package-name', '?')
                    if nr[0] == 'Axure:Page':
                        page_guids.append(guid)

        # 每个页面 blob 的包 GUID = 其 GUID 池[1]
        blob_guid = {}
        for rel, dec in self.pages:
            try:
                d = rf.decode_blob(dec, 'p')
                if len(d['pool']) > 1:
                    blob_guid[rel] = rf.format_guid(d['pool'][1])
            except Exception:
                pass
        page_by_guid = {}
        used = set()
        for g in page_guids:
            for rel, bg in blob_guid.items():
                if bg == g and rel not in used:
                    page_by_guid[g] = rel
                    used.add(rel)
                    break
        # 未匹配的页面按排除法分配
        leftover = [rel for rel, _ in self.pages if rel not in used]
        leftover_names = [g for g in page_guids if g not in page_by_guid]
        for g in leftover_names:
            if leftover:
                page_by_guid[g] = leftover.pop(0)

        items = []
        if self.dd:
            stm = None
            for o in objs.values():
                if o.get('__type__') == 'Axure:SiteTreeMap':
                    stm = o
                    break
            if stm:
                node_table = stm.get('node-table', {})
                tree = rf.resolve_refs(stm.get('tree'), objs)
                self._walk_tree(tree, 0, node_table, pool, guid2name, items)
        return items, page_by_guid

    def _walk_tree(self, node, depth, node_table, pool, guid2name, items):
        if not isinstance(node, dict):
            return
        nid = node.get('__id__')
        guid = None
        ent = node_table.get('#obj%d' % nid) if nid is not None else None
        if isinstance(ent, dict) and ent.get('__named_ref__'):
            n = ent['__named_ref__'][1]
            guid = pool[n - 1] if isinstance(n, int) and 0 < n <= len(pool) else None
        kids = node.get('children') or []
        items.append({'depth': depth, 'name': guid2name.get(guid, '页面 %s' % nid),
                      'guid': guid, 'folder': bool(kids), 'node_id': nid})
        for c in kids:
            self._walk_tree(c, depth + 1, node_table, pool, guid2name, items)

    # ---------------- widget rendering ----------------
    def save_image(self, val):
        if not isinstance(val, dict):
            return None
        stream = val.get('Stream')
        if not isinstance(stream, bytes) or not stream:
            return None
        ext = val.get('Extension') or '.png'
        ext = '.' + str(ext).lower().lstrip('.')
        if ext not in ('.png', '.jpg', '.jpeg', '.gif', '.bmp', '.webp'):
            self.warn.append('跳过不支持的图片扩展名: %s' % ext)
            return None
        key = hashlib.sha1(stream).hexdigest()[:16]
        if key not in self.img_written:
            fname = key + ext
            d = os.path.join(self.out_dir, 'images')
            if not os.path.isdir(d):
                os.makedirs(d)
            with open(os.path.join(d, fname), 'wb') as f:
                f.write(stream)
            self.img_written[key] = fname
        return '../images/' + self.img_written[key]

    def widget_box(self, w):
        pm = prop_map(w.get('MyStyle'))
        loc = pm.get(HASH_LOCATION) or {}
        size = pm.get(HASH_SIZE) or {}
        try:
            x = float(loc.get('X', 0) or 0)
            y = float(loc.get('Y', 0) or 0)
            wd = float(size.get('Width', 0) or 0)
            ht = float(size.get('Height', 0) or 0)
        except Exception:
            return None
        if wd <= 0 or ht <= 0:
            return None
        return x, y, wd, ht

    def widget_fill_css(self, w):
        fill = prop_map(w.get('MyStyle')).get(HASH_FILL)
        if isinstance(fill, dict):
            if fill.get('fill-type') == 'none':
                return None
            return argb_css(fill.get('fill-color'))
        return None

    def widget_image(self, w):
        pm = prop_map(w.get('MyStyle'))
        if HASH_IMAGE in pm:
            return self.save_image(pm.get(HASH_IMAGE))
        return None

    def widget_html(self, w):
        parts = []
        kids = w.get('diagram-objects')
        if isinstance(kids, list):
            for k in kids:
                if isinstance(k, dict):
                    parts.append(self.widget_html(k))
        rtxt = w.get('RichTextBlock')
        if isinstance(rtxt, dict):
            parts.append(self.rich_text_html(rtxt))
        else:
            tb = w.get('TextBlock')
            if isinstance(tb, dict) and tb.get('text'):
                parts.append('<div class="tx">%s</div>' % html.escape(str(tb['text'])))
        return ''.join(parts)

    def rich_text_html(self, rtxt):
        ast = rtxt.get('attributed-string')
        if not isinstance(ast, dict):
            return ''
        chunks = []
        for para in ast.get('Paragraphs', []) or []:
            runs = []
            for inl in para.get('Inlines', []) or []:
                if not isinstance(inl, dict):
                    continue
                txt = inl.get('Text')
                if not txt:
                    continue
                st = []
                if inl.get('Family'):
                    st.append('font-family:%s' % html.escape(str(inl['Family'])))
                sz = inl.get('Size')
                if isinstance(sz, (int, float)) and sz:
                    st.append('font-size:%.1fpx' % (float(sz) * 4.0 / 3.0))
                wt = inl.get('Weight')
                if isinstance(wt, (int, float)) and wt and wt != 400:
                    st.append('font-weight:%d' % int(wt))
                if inl.get('Underline'):
                    st.append('text-decoration:underline')
                if inl.get('Strikethrough'):
                    st.append('text-decoration:line-through')
                col = argb_css(inl.get('Color')) if isinstance(inl.get('Color'), int) else None
                if col and inl.get('Color') != -1:
                    st.append('color:%s' % col)
                sty = (' style="%s"' % ';'.join(st)) if st else ''
                runs.append('<span%s>%s</span>' % (sty, html.escape(str(txt))))
            if runs:
                chunks.append('<div class="para">%s</div>' % ''.join(runs))
        return ''.join(chunks)

    def render_page(self, seq, dec):
        d = rf.decode_blob(dec, 'page%d' % seq)
        if d['warnings']:
            self.warn.append('页面 %d: %s' % (seq, d['warnings'][0]))
        objs = d['objects']
        diag = rf.resolve_refs(d['root'].get('diagram'), objs)
        widgets = diag.get('diagram-objects', []) if isinstance(diag, dict) else []

        body = []
        max_x = max_y = 0.0
        for w in widgets:
            if not isinstance(w, dict):
                continue
            if w.get('VisibleFromSettings') is False:
                continue
            box = self.widget_box(w)
            if box is None:
                continue
            x, y, wd, ht = box
            max_x = max(max_x, x + wd)
            max_y = max(max_y, y + ht)
            inner = self.widget_html(w)
            style = ['position:absolute', 'left:%gpx' % x, 'top:%gpx' % y,
                     'width:%gpx' % wd, 'height:%gpx' % ht,
                     'box-sizing:border-box', 'overflow:hidden']
            fill = self.widget_fill_css(w)
            img = self.widget_image(w)
            if img:
                style.append('background:#fff')
            elif fill:
                style.append('background:%s' % fill)
            body.append('<div class="w" id="w%d" style="%s">%s%s</div>' % (
                w.get('__id__', 0), ';'.join(style),
                '<img src="%s" style="width:100%%;height:100%%;display:block">' % img if img else '',
                inner))
        return max(max_x, 100), max(max_y, 100), ''.join(body)

    # ---------------- html output ----------------
    def write_site(self):
        out = self.out_dir
        for sub in ('pages', 'thumbs'):
            dd_ = os.path.join(out, sub)
            if not os.path.isdir(dd_):
                os.makedirs(dd_)
        items, page_by_guid = self.build_outline()

        # 索引: 包偏移 -> 缩略图偏移 (精确配对)
        thumb_for_page = {}
        if self.index:
            children = self.index.get('root', {}).get('children', {})
            pkg2thumb = {}
            for h, parts in children.items():
                p = parts.get('parts', {})
                if isinstance(p.get('package'), int) and isinstance(p.get('thumbnail'), int):
                    pkg2thumb[p['package']] = p['thumbnail']
            for rel in self.rel:
                if rel in pkg2thumb and pkg2thumb[rel] in self.rel:
                    thumb_for_page[rel] = pkg2thumb[rel]

        ordered = []
        used = set()
        for it in items:
            rel = page_by_guid.get(it['guid'])
            if rel is None or rel in used:
                continue
            used.add(rel)
            ordered.append((it['name'], rel, it['depth']))
        for rel, _ in self.pages:
            if rel not in used:
                ordered.append(('页面 %d' % (len(ordered) + 1), rel, 0))

        page_meta = []
        seq_by_rel = {rel: i for i, (rel, _) in enumerate(self.pages)}
        blob_by_rel = dict(self.pages)
        for name, rel, depth in ordered:
            seq = seq_by_rel[rel]
            cw, ch, body_html = self.render_page(seq, blob_by_rel[rel])
            page_meta.append({'seq': seq, 'name': name, 'w': cw, 'h': ch})
            with open(os.path.join(out, 'pages', 'p%d.html' % seq), 'w', encoding='utf-8') as f:
                f.write(PAGE_TMPL % {'title': html.escape(name),
                                     'w': '%g' % cw, 'h': '%g' % ch,
                                     'body': body_html})

        items_html = []
        for name, rel, depth in ordered:
            seq = seq_by_rel[rel]
            trel = thumb_for_page.get(rel)
            thumb = 'thumbs/t%d.jpg' % seq if trel is not None else ''
            th = ('<img class="th" src="%s" alt="">' % thumb) if thumb else ''
            items_html.append(
                '<div class="item" style="padding-left:%dpx">%s<a href="pages/p%d.html" target="view">%s</a></div>'
                % (8 + depth * 14, th, seq, html.escape(name)))
        with open(os.path.join(out, 'index.html'), 'w', encoding='utf-8') as f:
            f.write(INDEX_TMPL % {'title': html.escape(os.path.basename(self.rp_path)),
                                  'items': ''.join(items_html),
                                  'first': ('pages/p%d.html' % seq_by_rel[ordered[0][1]]) if ordered else 'about:blank'})
        with open(os.path.join(out, 'pages.json'), 'w', encoding='utf-8') as f:
            json.dump(page_meta, f, ensure_ascii=False, indent=1)

        # 缩略图文件 (按页面序号)
        for name, rel, depth in ordered:
            seq = seq_by_rel[rel]
            trel = thumb_for_page.get(rel)
            if trel is not None:
                with open(os.path.join(out, 'thumbs', 't%d.jpg' % seq), 'wb') as f:
                    f.write(self.thumbs[trel])


PAGE_TMPL = """<!DOCTYPE html>
<html lang="zh"><head><meta charset="utf-8"><title>%(title)s</title>
<style>
body{margin:0;background:#e8e8e8;font-family:'Microsoft YaHei',Arial,sans-serif}
#c{position:relative;background:#fff;margin:16px auto 0;box-shadow:0 2px 12px rgba(0,0,0,.25);transform-origin:0 0}
.w{position:absolute}
.tx,.para{white-space:pre-wrap;word-break:break-word}
</style></head>
<body><div id="wrap"><div id="c" style="width:%(w)spx;height:%(h)spx">%(body)s</div></div>
<script>
function fit(){var c=document.getElementById('c'),w=document.getElementById('wrap');
var s=Math.min(1,(window.innerWidth-48)/c.offsetWidth);c.style.transform='scale('+s+')';
w.style.height=(c.offsetHeight*s+24)+'px';}
window.addEventListener('resize',fit);fit();
</script></body></html>
"""

INDEX_TMPL = """<!DOCTYPE html>
<html lang="zh"><head><meta charset="utf-8"><title>%(title)s - Axure 原型查看器</title>
<style>
body{margin:0;font-family:'Microsoft YaHei',Arial,sans-serif;display:flex;height:100vh}
#side{width:280px;overflow:auto;background:#2b3137;color:#dfe3e6;padding:10px 0}
#side h1{font-size:14px;margin:6px 12px;color:#fff;word-break:break-all}
.item{display:flex;align-items:center;padding:4px 10px;font-size:13px}
.item a{color:#dfe3e6;text-decoration:none;flex:1}
.item a:hover{color:#ffcf5c}
.th{width:44px;height:28px;object-fit:cover;margin-right:8px;border:1px solid #555;background:#fff}
#main{flex:1;display:flex}
iframe{border:0;width:100%%;height:100%%;background:#e8e8e8}
</style></head>
<body><div id="side"><h1>%(title)s</h1>%(items)s</div>
<div id="main"><iframe name="view" src="%(first)s"></iframe></body></html>
"""


def main():
    ap = argparse.ArgumentParser(description='解析 Axure RP9 .rp 文件并生成可浏览的 HTML')
    ap.add_argument('rpfile', help='.rp 文件路径')
    ap.add_argument('-o', '--out', default=None, help='输出目录 (默认: <rpfile>_view)')
    args = ap.parse_args()
    out = args.out or (os.path.splitext(args.rpfile)[0] + '_view')
    v = Viewer(args.rpfile, out)
    v.load()
    v.write_site()
    print('OK: output -> %s' % os.path.abspath(out))
    for w in v.warn:
        print('WARNING: ' + w)


if __name__ == '__main__':
    main()
