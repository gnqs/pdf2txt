all: read_pdf  store_txt  convert_pdf
	mkdir -p tmp
	mkdir -p logs

read_pdf: read_pdf.c
	mkdir -p bin
	$(CC) read_pdf.c -o ./bin/read_pdf

convert_pdf: convert_pdf.c hooktest.h
	$(CC) convert_pdf.c -o ./bin/convert_pdf

store_txt: store_txt.c
	$(CC) store_txt.c -o ./bin/store_txt

clean: 
	rm -f ./bin/convert_pdf ./bin/read_pdf ./bin/store_txt
	rm -rf ./tmp ./logs