pdf2txt服务分成三个模块
service1：read_pdf.c (读取pdf文件,向servce2发送pdf)
service2: convert_pdf.c (接收service1发送来的pdf，调用xpdf中的pdftotext进行格式转化，并将得到的txt发往service3)
service3: store_txt.c (接收service2发送来的txt并保存在本地)

一、基本运行：
make 
./bin/read_pdf [path/to/pdf_file]
./bin/convert_pdf
./bin/store_txt


二、进行模糊测试
0. 准备
用mutifuzz_update0712文件夹中的client.c 和 interface.c替换原文件
确保已经编译好 afl-2.52b，client 以及interface

1. 编译插桩
#在pdf2txt/目录下执行命令，path/to均需更换为实际路径

    export CC=[path/to]/afl-2.52b/afl-gcc
    export CXX=[path/to]/afl-2.52b/afl-g++
    make
    cd xpdf-3.02
    make


2. 运行
#在pdf2txt/目录下执行命令，path/to均需更换为实际路径

    [path/to]/client 9000 ./bin/read_pdf -f
    [path/to]/client 9001 ./bin/convert_pdf
    [path/to]/client 9002 ./bin/store_txt


    [path/to]/interface -n 3 pdf_example/helloworld.pdf
    如果正常执行，可进行下一步，否则根据报错，检查文件路径等信息

    新建in_dir，放入初始pdf文件
    mkdir in_dir
    [move pdf_files to in_dir]
    [path/to]/afl-2.52b/afl-fuzz -i in_dir -o out_dir -- [path/to]/interface -n 3 @@


3. 如果要调整组件的部署位置以及端口，需要进行相应修改

需修改 read_pdf.c Line8,9的ip和port
修改convert_pdf.c Line14-16的listen_port,ip以及port
修改store_txt.c Line10的listen_port

interface的运行参数为: 
  interface -n [num] [file]
  num为组件数量，具体每个组件的ip和port的interface.c 的Line45-53修改
  默认ip均为127.0.0.1，端口为9000，9001，9002...

client的运行参数为：
    client [port] [command to run a component] [-f] 
    其中port对应interface中设定的端口，-f选项是可选的，标识该组件是否接收文件
    注意必须将接收文件的那一个组件的端口设定为9000，也即-f选项必须和端口9000的组件在一起，这个组件也必须是需要读文件的
