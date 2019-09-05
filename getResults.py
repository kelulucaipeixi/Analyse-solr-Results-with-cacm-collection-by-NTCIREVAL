import urllib.request
import re

def getHtml(url):
	page=urllib.request.urlopen(url)
	html=page.read()
	return html
def getId(html):
	reg1=r'(?<="id":")\d*'
	reg2=r'(?<="score":)\d+\.?\d*'
	Ids=re.compile(reg1)
	scores=re.compile(reg2)
	IdList=re.findall(Ids,html.decode('utf-8'))
	scoreList=re.findall(scores,html.decode('utf-8'))
	# print(len(IdList)
	return IdList,scoreList
def write(id,Ids,scores,f):
	for i in range(len(scores)):
		line=str(id.zfill(4))+" Q0 "+str(Ids[i].zfill(4))+" "+str(i+1)+" "+scores[i]+" cacm"
		f.write(line+"\n")
fq=open("./cacm/query.text")
qlines=fq.readlines()
flag=0
id=1
tmp=0
wordList=""
f=open("./query_results",'w')
for line in qlines:
	# tmp+=1
	# if tmp>50:break
	# print(id)
	if line[0]=='.' and line[1]=='I':
		flag=1
		id=line[3:]
		continue
	if id=='30\n' or id=='33\n' or id=='35\n' or id=='38\n' or id=='40\n' or id=='44\n' or id=='57\n' or id =='64\n':
	 	continue
	if line[0]=='.' and line[1]=='W':
		flag=2
		continue
	if flag==2 and line[0]=='.' and line[1]!='W':
		wordList=wordList.replace(" ","%20").replace("\n","").replace(",","%2C").replace(":","%3A").replace('"',"%22")
		url="http://localhost:8983/solr/cacm/select?fl=*%2Cscore&q="+wordList+"&rows=3000"
		# print(url)
		# url="http://localhost:8983/solr/cacm/select?fl=*%2Cscore&q=Articles%20on%20text%20formatting%20systems%2C%20including%20%22what%20you%20see%20is%20what%20you%0Aget%22%20systems.%20%20Examples%3A%20t%2Fnroff%2C%20scribe%2C%20bravo."
		# url="http://localhost:8983/solr/cacm/select?fl=*%2Cscore&q=Number-theoretic%20algorithms%2C%20especially%20involving%20prime%20number%20series%2C%0Asieves%2C%20and%20Chinese%20Remainder%20theorem.&rows=200"
		# url="http://localhost:8983/solr/cacm/select?fl=*%2Cscore&q=Articles%20on%20text%20formatting%20systems%2C%20including%20%22what%20you%20see%20is%20what%20you%0Aget%22%20systems.%20%20Examples%3A%20t%2Fnroff%2C%20scribe%2C%20bravo.&rows=200"
		# url="http://localhost:8983/solr/cacm/select?fl=*%2Cscore&q=Articles%20on%20text%20formatting%20systems%2C%20including%20%22what%20you%20see%20is%20what%20you%0Aget%22%20systems.%20%20Examples%3A%20t%2Fnroff%2C%20scribe%2C%20bravo."
		ht=getHtml(url)
		Ids,scores=getId(ht)
		id=id.replace("\n","")
		write(id,Ids,scores,f)
		
		wordList=""
		flag=0
		continue
	if flag==2:
		wordList+=line

f.close()




# url="http://localhost:8983/solr/cacm/select?fl=*%2Cscore&q=Extraction%20of%20Roots%20by%20Repeated%20Subtractions%20for%20Digital%20Computers"
# ht=getHtml(url)
# Ids,scores=getId(ht)
# f=open("./query_results",'w')

# write(id,Ids,scores,f)
# f.close()