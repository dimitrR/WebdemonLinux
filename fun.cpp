bool videodevice::readFrame(string file_name){
    struct v4l2_buffer buf;
    buf.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory=V4L2_MEMORY_MMAP;

    //освобождаем буфер
    if(xioctl(fd, VIDIOC_DQBUF, &buf)==EAGAIN)
        return false;

    buffer *temp=devbuffer;

    //сохраняем в файл RAW
    string rawFile=file_name+string(".raw");
    FILE *out_file=fopen(rawFile.c_str(), "w");
    fwrite(temp->start, temp->length, 1, out_file);
    fclose(out_file);

    //сохраняем фото в jpg
    cout<<"save jpeg"<<endl;
    string jpegFile=file_name+string(".jpeg");
    savetojpeg(jpegFile.c_str());

    return true;
}

unsigned char* videodevice::YUVtoRGB(unsigned char *buffer){
    unsigned char *rgbBuffer=(unsigned char*)malloc(IMAGE_WIDTH*IMAGE_HEIGHT*3+1);

    char lnPixel=2;

    for(int i=0; i<IMAGE_HEIGHT/8; i++){                            //блоки по вертикале
        for(int j=0; j<IMAGE_WIDTH/8; j++){                         //блоки по горизонтале
            int imgData=((i*8)*IMAGE_WIDTH+j*8)*lnPixel;            //накальная координата блока
            for(int k=0; k<8; k++){                                 //пиксели по вертикале
                for(int l=0; l<8; l++){                             //пиксели по горизонтале
                    double Y=buffer[imgData+k*IMAGE_WIDTH*lnPixel+l*lnPixel];
                    double Cb=buffer[imgData+k*IMAGE_WIDTH*lnPixel+l*lnPixel+1];
                    double Cr=buffer[imgData+k*IMAGE_WIDTH*lnPixel+l*lnPixel+2];

                    double r=Y+Cr-128;
                    double b=Y+Cb-128;
                    double g=Y-r-b;

                    if(r>255) r=255;
                    else if(r<0) r=0;
                    if(g>255) g=255;
                    else if(g<0) g=0;
                    if(b>255) b=255;
                    else if(b<0) b=0;

                    rgbBuffer[imgData+k*IMAGE_WIDTH*lnPixel+l*lnPixel]=r;
                    rgbBuffer[imgData+k*IMAGE_WIDTH*lnPixel+l*lnPixel+1]=g;
                    rgbBuffer[imgData+k*IMAGE_WIDTH*lnPixel+l*lnPixel+2]=b;
                }
            }
        }
    }

    return rgbBuffer;
}

void videodevice::savetojpeg(const char *filename){
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    FILE *outfile;                //выходной файл jpeg
    JSAMPROW row_pointer[1];      //указатель на JSAMPLE

    //инициализация jpeg компресора
    cinfo.err=jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    //открытия файла для сохраннения
    if((outfile=fopen(filename, "wb")) == NULL) {
        fprintf(stderr, "can't open %s\n", filename);
        exit(1);
    }

    jpeg_stdio_dest(&cinfo, outfile);
    cinfo.image_width=IMAGE_WIDTH;         //размер изображения
    cinfo.image_height=IMAGE_HEIGHT;
    cinfo.input_components=3;              //количество цветовых компанентов
    cinfo.in_color_space=JCS_RGB;          //цветовая схема

    //установка параметров изображения
    jpeg_set_defaults(&cinfo);

    //установка качества изображения
    jpeg_set_quality(&cinfo, 200, TRUE);

    //Начало компрессии изображения
    jpeg_start_compress(&cinfo, TRUE);

    int row_stride=IMAGE_WIDTH;

    //unsigned char *bufferRGB=YUVtoRGB((unsigned char*)devbuffer->start);
    //JSAMPLE *image_buffer=(JSAMPLE*)bufferRGB;
    JSAMPLE *image_buffer=(JSAMPLE*)devbuffer->start;
    while (cinfo.next_scanline<cinfo.image_height) {
        //запись построчна
        row_pointer[0]=&image_buffer[cinfo.next_scanline*row_stride];
        (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    fclose(outfile);
    jpeg_destroy_compress(&cinfo);
}
