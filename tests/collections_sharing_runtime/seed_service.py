"""Create synthetic runtime owner and fixed gift through the actual service."""
import json,uuid,secrets,urllib.request,sys
from pathlib import Path
base='http://127.0.0.1:55440';evidence=Path(sys.argv[1]); repo=Path(__file__).resolve().parents[2]
def request(path,body,token=None):
    req=urllib.request.Request(base+path,json.dumps(body).encode(),{'Content-Type':'application/json',**({'Authorization':'Bearer '+token} if token else {})})
    return json.load(urllib.request.urlopen(req,timeout=30))
username='native'+secrets.token_hex(4);password='Synthetic!'+secrets.token_hex(18)
a=request('/v1/accounts',{'username':username,'password':password,'device_install_id':str(uuid.uuid4()),'device_label':'Disposable native fixture seed','platform':'linux'})
fixture=json.loads((repo/'tests/collections_sharing/fixtures/mixed-gift-v1.json').read_text())
for m in fixture['members']:
    m.setdefault('album_source_id','');m.setdefault('album_browse_id','');m.setdefault('comic_post_ids',[])
gift=request('/v1/collections/publications',{'operation_id':str(uuid.uuid4()),'kind':'gift','snapshot':fixture,'passphrase':''},a['session']['access_token'])
# Ephemeral, deliberately excluded from publishable receipts. Tokens not needed.
(evidence/'synthetic-seed-private.json').write_text(json.dumps({'username':username,'password':password,'gift':gift}))
(evidence/'seed-public.json').write_text(json.dumps({'username':username,'share_id':gift['share_id'],'service':base,'snapshot':gift['snapshot']},indent=2))
