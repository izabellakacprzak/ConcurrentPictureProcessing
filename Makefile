all: blur_opt_exprmt

blur_opt_exprmt: BlurExprmt.o Utils.o Picture.o PicProcess.o
	gcc sod_118/sod.c BlurExprmt.o Utils.o Picture.o PicProcess.o -I sod_118 -lm -lpthread -o blur_opt_exprmt

Utils.o: Utils.h Utils.c

Picture.o: Utils.h Picture.h Picture.c

PicProcess.o: Utils.h Picture.h PicProcess.h PicProcess.c

BlurExprmt.o: BlurExprmt.c Utils.h Picture.h PicProcess.h

%.o: %.c
	gcc -c -I sod_118 -lm -lpthread $<

clean:
	rm -rf picture_lib concurrent_picture_lib blur_opt_exprmt picture_compare *.o

.PHONY: all clean

