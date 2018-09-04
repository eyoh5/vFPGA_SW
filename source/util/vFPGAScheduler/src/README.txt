==== how to compile
$make // makefile will generate realted '.o' files

$g++ -fPIC -m64 -o scheduler ./vfpgaschedul
er.o ./mmdapp.o ./main.o -L/home/ncl/intelFPGA_pro/17.1/hld/board/a10_test/linux64/lib -L/home/ncl/intelFPGA_pro/17.1/hld/host/linux64/lib -z noexecstack -Wl,-z,relro,-z,now -Wl,-Bsymbolic -fPIC -Wl,--no-undefined -Wl,--exclude-libs,ALL -m64  -lalteracl -lelf -lalterahalmmd -laltera_a10_test_mmd -pthread
