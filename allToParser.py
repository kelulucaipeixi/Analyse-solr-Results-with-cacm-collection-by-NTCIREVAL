import os
import xml.dom.minidom

f=open("./cacm/cacm.all")
lines=f.readlines()

doc=xml.dom.minidom.Document()
# annotation=doc.createElement('annotation')
# doc.appendChild(annotation)
add=doc.createElement('add')
doc.appendChild(add)
# doc2=doc.createElement('doc')
# add.appendChild(doc2)
nodes={0:add}
print(nodes)
index=0
flag=""
abstrats=""
for line in lines:
	if index<2700:
		if line[0]=='.' and line[1]=='I':
			index+=1
			nodes[index]=doc.createElement('doc')
			add.appendChild(nodes[index])
			node=doc.createElement('field')
			node.setAttribute('name','id')
			textValue=doc.createTextNode(line[3:])
			node.appendChild(textValue)
			nodes[index].appendChild(node)
			continue
		if line[0]=='.' and line[1]=='W': 
			flag='Abstrat'
			continue
		if flag=='Abstrat' and line[0]!='.':
			abstrats+=line
			continue
		elif flag=='Abstrat' and line[0]=='.':
			node=doc.createElement('field')
			node.setAttribute('name','Abstrat')
			textValue=doc.createTextNode(abstrats)
			node.appendChild(textValue)
			nodes[index].appendChild(node)
			abstrats=""
		if line[0]=='.' and line[1]=='T': 
			flag='title' 
			continue
		if line[0]=='.' and line[1]=='B': 
			flag='date' 
			continue
		if line[0]=='.' and line[1]=='A': 
			flag='authors' 
			continue
		if line[0]=='.' and line[1]=='N': 
			flag='entry_date' 
			continue
		if line[0]=='.' and line[1]=='X': 
			flag='references' 
			continue
		node=doc.createElement('field')
		node.setAttribute('name',flag)
		textValue=doc.createTextNode(line)
		node.appendChild(textValue)
		nodes[index].appendChild(node)

filename="./cacm/results.xml"
# with open(filename, 'w') as f:
# 	f.write(doc.toprettyxml(indent='\t'))
with open(filename, 'w') as f:
    f.write(doc.toprettyxml(indent='\t'))