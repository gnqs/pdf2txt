服务分成三个模块
service1：read_pdf.c (读取pdf文件,向servce2发送pdf)
service2: convert_pdf.c (接收service1发送来的pdf，调用xpdf中的pdftotext进行格式转化，并将得到的txt发往service3)
service3: store_txt.c (接收service2发送来的txt并保存在本地)



export CC="/home/gnq/code/multifuzz/afl-2.52b/afl-gcc"
export CXX="/home/gnq/code/multifuzz/afl-2.52b/afl-g++"

# /home/gnq/code/multifuzz/client 9000 /home/gnq/code/pdf2txt/xpdf-3.02/xpdf/pdftotext -f 直接fuzz xpdf


/home/gnq/code/multifuzz/client 9000 /home/gnq/code/pdf2txt/bin/read_pdf -f
/home/gnq/code/multifuzz/client 9001 /home/gnq/code/pdf2txt/bin/convert_pdf
/home/gnq/code/multifuzz/client 9002 /home/gnq/code/pdf2txt/bin/store_txt


# ./interface /home/gnq/code/pdf2txt/helloworld.pdf