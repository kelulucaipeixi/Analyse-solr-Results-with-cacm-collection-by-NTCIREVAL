CC   =gcc  -g -O -c -o $@ -DOUTERR
LC   =gcc -o $@
DEST =/usr/local/bin

ntcir_eval: ntcir_eval.o
	$(LC) ntcir_eval.o -lm

ntcir_eval.o: ntcir_eval.c ntcir_eval.h
	$(CC) ntcir_eval.c

install: ntcir_eval
		 install -s ntcir_eval $(DEST)
		 @printf "Please execute 'export PATH=/usr/local/bin:\$$PATH' if necessary\n"
uninstall:
		 rm $(DEST)/ntcir_eval
