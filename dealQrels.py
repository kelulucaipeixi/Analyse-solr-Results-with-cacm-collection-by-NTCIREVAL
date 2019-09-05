import os

f=open("./cacm/qrels.text")
des=open("qrels","w")
lines=f.readlines()
for line in lines:
	line=line[0:8].zfill(10)+'L1'
	des.write(line+'\n')
f.close()
des.close()
