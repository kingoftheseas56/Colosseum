from pathlib import Path
b=Path(__file__).parent/'finished-mocks'
p=b/'17-theatre-details/style.css';s=p.read_text(encoding='utf-8')
lines=s.splitlines();assert any('glyph-collection {' in x for x in lines)
p.write_text('\n'.join(x for x in lines if 'glyph-collection {' not in x)+'\n',encoding='utf-8')
p=b/'18-comic-details/app.js';s=p.read_text(encoding='utf-8')
old="</span></div>'+[["
new="</span></div><div class=\"source-result-list\">'+[["
assert old in s;s=s.replace(old,new,1)
old="</span></button>').join(\"\");\n return '<div class=\"source-table-head\""
new="</span></button>').join(\"\")+'</div>';\n return '<div class=\"source-table-head\""
assert old in s;s=s.replace(old,new,1)
old="Torrents-CSV</span></div>'+results.map"
new="Torrents-CSV</span></div><div class=\"source-result-list\">'+results.map"
assert old in s;s=s.replace(old,new,1)
old="</button></div>').join(\"\");\n}\nfunction sourceOverlay"
new="</button></div>').join(\"\")+'</div>';\n}\nfunction sourceOverlay"
assert old in s;s=s.replace(old,new,1);p.write_text(s,encoding='utf-8')
p=b/'18-comic-details/style.css'
with p.open('a',encoding='utf-8') as f:f.write("\n/* Fit the existing source page below Back on Android; preserve its pinned desktop banner/table. */\n.app.android-host .source-scroll{position:absolute;inset:80px 0 0;height:auto}\n.app.android-host .source-overlay:before{height:80px;background:#000}\n.app.android-host .source-banner{min-height:220px;padding-top:14px}\n@container(max-width:600px){.app.android-host .source-banner{min-height:180px;padding-top:0}}\n.app.desktop .source-overlay:before{display:none}\n.app.desktop .source-scroll{display:flex;flex-direction:column;overflow:hidden}\n.app.desktop .source-banner{height:300px;min-height:300px;flex:0 0 300px}\n.app.desktop .source-subhead,.app.desktop .source-heading .gold-eyebrow{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}\n.app.desktop .source-body{display:flex;flex-direction:column;min-height:0;flex:1;overflow:hidden}\n.app.desktop .source-query{height:56px;flex:0 0 56px}\n.app.desktop .source-body:not(:has(.source-query)):before{content:\"\";height:56px;flex:0 0 56px}\n.app.desktop .source-table{display:flex;flex-direction:column;min-height:0;flex:1;overflow:hidden}\n.app.desktop .source-table-head{flex:none}\n.app.desktop .source-result-list{min-height:0;flex:1;overflow:auto;overscroll-behavior:contain}\n.app.desktop .source-table>.pack-state,.app.desktop .source-table>.pack-empty{min-height:0;flex:1;overflow:auto}\n")
print('Removed obsolete missing icon URL; fitted source Back boundary and original desktop source table.')
