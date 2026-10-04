#include "webFilesUi.h"

namespace WebFilesUi {

const char* indexHtml() {
    return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Diptyx Web Files</title>
<style>
body{font-family:system-ui,-apple-system,sans-serif;max-width:720px;margin:0 auto;padding:18px;line-height:1.35}
h1{margin:0 0 4px}.muted{color:#666}.book{display:flex;gap:10px;align-items:center;padding:12px 0;border-bottom:1px solid #ddd}.name{flex:1;word-break:break-word}.size{color:#666;font-size:.9em;white-space:nowrap}button{padding:8px 12px;font-size:1em}#status{min-height:1.5em;margin:12px 0}.danger{background:#f6d7d7}.top{display:flex;justify-content:space-between;align-items:center;gap:10px}.storage{margin:10px 0 18px;padding:10px;background:#f4f4f4}.actions{display:flex;gap:8px;flex-wrap:wrap}
</style>
</head>
<body>
<div class="top"><h1>Diptyx</h1><button onclick="stopWeb()">Stop</button></div>
<div class="muted">SD card ebooks</div>
<div id="status"></div>
<div class="storage" id="storage">Loading storage…</div>
<div class="actions"><input id="file" type="file" accept=".epub,application/epub+zip"><button onclick="upload()">Upload EPUB</button></div>
<h2>Books</h2>
<div id="books">Loading…</div>
<script>
const $=id=>document.getElementById(id);
function setStatus(t){$('status').textContent=t;}
function fmt(n){const u=['B','KB','MB','GB','TB'];let i=0;let v=Number(n);while(v>=1024&&i<u.length-1){v/=1024;i++;}return v.toFixed(i?1:0)+' '+u[i];}
async function load(){
  const [files,store]=await Promise.all([fetch('/api/files',{cache:'no-store'}),fetch('/api/storage',{cache:'no-store'})]);
  const list=await files.json(); const s=await store.json();
  $('storage').textContent=`Free ${fmt(s.free)} / ${fmt(s.total)}`;
  if(!list.length){$('books').textContent='No EPUB files found.';return;}
  $('books').innerHTML='';
  for(const f of list){
    const row=document.createElement('div'); row.className='book';
    const n=document.createElement('div'); n.className='name'; n.textContent=f.name;
    const z=document.createElement('div'); z.className='size'; z.textContent=fmt(f.size);
    const d=document.createElement('a'); d.href='/api/files/'+encodeURIComponent(f.name); d.textContent='Download'; d.download=f.name;
    const b=document.createElement('button'); b.className='danger'; b.textContent='Delete'; b.onclick=()=>del(f.name);
    row.append(n,z,d,b); $('books').appendChild(row);
  }
}
async function upload(){
  const f=$('file').files[0]; if(!f){setStatus('Choose an EPUB first.');return;}
  if(!/\.epub$/i.test(f.name)){setStatus('Only EPUB files are supported.');return;}
  setStatus('Uploading '+f.name+'…');
  let r=await fetch('/api/files/'+encodeURIComponent(f.name),{method:'PUT',body:f});
  if(r.status===409 && confirm('That book already exists. Replace it?')){
    r=await fetch('/api/files/'+encodeURIComponent(f.name)+'?replace=1',{method:'PUT',body:f});
  }
  if(!r.ok){setStatus(await r.text());return;}
  $('file').value=''; setStatus('Upload complete.'); await load();
}
async function del(name){
  if(!confirm('Delete '+name+'?'))return;
  const r=await fetch('/api/files/'+encodeURIComponent(name),{method:'DELETE'});
  if(!r.ok){setStatus(await r.text());return;}
  setStatus('Deleted '+name); await load();
}
async function stopWeb(){
  setStatus('Closing web mode…');
  try{await fetch('/api/stop',{method:'POST'});}catch(e){}
}
load().catch(e=>setStatus('Unable to load file list.'));
</script>
</body>
</html>)HTML";
}

}
