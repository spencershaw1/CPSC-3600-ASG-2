default: udping.c AddressUtility.c DieWithMessage.c 
	gcc -pthread -o udping udping.c AddressUtility.c DieWithMessage.c