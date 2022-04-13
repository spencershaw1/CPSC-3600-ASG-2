# SPENCER SHAW (JSSHAW) AND ADAM COPELAND (AKCOPEL)
default: udping.c AddressUtility.c DieWithMessage.c 
	gcc -pthread -o udping udping.c AddressUtility.c DieWithMessage.c

clean:
	-rm -f udping