import re,sys,statistics as st
labels=[f"{v} dc" for v in "10 5 2 1 0.5 0.2 0.1 0.05 0.02 0.01".split()]
for v in ["5","0.5","0.05"]:
    labels+= [f"{v} off{f}" for f in ["0.25","0.5","0.75"]]+[f"{v} ac"]
i=-1; d={}
for line in open(sys.argv[1]):
    if "Comando desconocido: ==" in line: i+=1; d[i]={"z":{}, "m":{1:[],2:[]}, "pp":{1:[],2:[]}}; 
    if i<0: continue
    m=re.search(r"Cero del canal (\d).*correccion (\S+) cuentas",line)
    if m: d[i]["z"][int(m[1])]=float(m[2])
    m=re.search(r"\[cal\] ch(\d) raw min=\d+ max=\d+ mean=(\S+) pp=(\d+)",line)
    if m: c=int(m[1]); d[i]["m"][c].append(float(m[2])); d[i]["pp"][c].append(int(m[3]))
    if "fallo" in line: print("FALLO", i, line.strip())
print(f"{'paso':14}{'z1':>8}{'z2':>8}  {'mean1':>8}{'sd1':>6}{'pp1':>5}  {'mean2':>8}{'sd2':>6}{'pp2':>5}")
for k in sorted(d):
    r=d[k]; f=lambda c:(f"{st.mean(r['m'][c]):8.2f}{(st.stdev(r['m'][c]) if len(r['m'][c])>1 else 0):6.2f}{max(r['pp'][c]):5d}" if r['m'][c] else " "*19)
    print(f"{labels[k]:14}{r['z'].get(1,float('nan')):8.2f}{r['z'].get(2,float('nan')):8.2f}  {f(1)}  {f(2)}")
