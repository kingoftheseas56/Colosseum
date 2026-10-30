"""Drive actual Lanista handlers through the build-gated synthetic file transport.
No QML evaluation, replacement stores, or network transport is introduced.
"""
import argparse,json,time,os,shutil
from pathlib import Path

def redact(value):
    if isinstance(value,dict):
        return {k:('[redacted]' if any(s in k.lower() for s in ('password','passphrase','access_token','refresh_token','recovery_key')) else redact(v)) for k,v in value.items()}
    if isinstance(value,list): return [redact(v) for v in value]
    return value

class Client:
    def __init__(self,directory,evidence):
        self.directory=Path(directory); self.evidence=Path(evidence); self.evidence.mkdir(parents=True,exist_ok=True)
        self.seq=max([int(p.name[:6]) for p in self.directory.glob('*.request.json')]+[0])+1
    def call(self,cmd,payload=None,timeout=40,allow_error=False):
        request={'seq':self.seq,'cmd':cmd,'payload':payload or {}}
        path=self.directory/f'{self.seq:06}.request.json'; replypath=self.directory/f'{self.seq:06}.reply.json'
        self.seq+=1; temp=path.with_suffix('.tmp');temp.write_text(json.dumps(request));os.replace(temp,path)
        end=time.monotonic()+timeout
        while not replypath.exists():
            if time.monotonic()>end: raise TimeoutError(f'No actual Lanista reply for {cmd}; {path}')
            time.sleep(.08)
        reply=json.loads(replypath.read_text()); record=redact({'request':request,'reply':reply})
        target=str((payload or {}).get('target',(payload or {}).get('object',''))).lower()
        if 'password' in target or 'passphrase' in target:
            record['request']['payload']['text']='[redacted]'
            if 'props' in record['reply']: record['reply']['props']['text']='[redacted]'
        with (self.evidence/'native-observed.jsonl').open('a') as f:f.write(json.dumps(record)+'\n')
        if not allow_error and reply.get('type')!='reply': raise RuntimeError(json.dumps(record))
        return reply
    def key(self,sequence):return self.call('ui-keypress',{'key':sequence})
    def click(self,target):return self.call('ui-click',{'target':target})
    def text(self,target,text):
        self.click(target);self.call('ui-keypress',{'key':'Ctrl+A'});return self.call('ui-text-input',{'target':target,'text':text})
    def props(self,target,*props):return self.call('qml-get',{'object':target,'props':list(props)})['props']
    def shot(self,name):
        reply=self.call('get-state',{'grab':{'target':'window'}})
        dest=self.evidence/(name+'.png');shutil.copyfile(reply['grabPath'],dest);return str(dest)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--evidence',required=True);p.add_argument('command');p.add_argument('payload',nargs='?',default='{}');a=p.parse_args()
    print(json.dumps(redact(Client(a.directory,a.evidence).call(a.command,json.loads(a.payload))),indent=2))
