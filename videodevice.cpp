#include <sys/ioctl.h>          //ioctrl()
#include <linux/videodev2.h>    //struct v4l2_capability
#include <unistd.h>             //close()

#include <sys/mman.h>
#include <sstream>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>


#include "videodevice.h"
#include "jpeglib.h"

#define IMAGE_WIDTH     800
#define IMAGE_HEIGHT    600

videodevice::videodevice(){
}

void videodevice::errno_exit(string err_str){
    cout<<"\""<<err_str<<"\": "<<errno<<", "<<strerror(errno)<<endl;
    exit(EXIT_FAILURE);
}

int videodevice::xioctl(int fd, int request, void *arg){
    int r;

    r=ioctl(fd, request, arg);
    if(r==-1){
        if(errno==EAGAIN)
            return EAGAIN;

        stringstream ss;
        ss<<"ioctl code "<<request<<" ";

        errno_exit(ss.str());
    }

    return r;
}

void videodevice::openDevice(string dev_name){
    fileDevicePath=dev_name;

    fd=open(fileDevicePath.c_str(), O_RDWR /* required */ | O_NONBLOCK, 0);
    if(fd==-1){
        stringstream str;
        str << "Cannot open '" << fileDevicePath << "'";
        errno_exit(str.str());
    }

    cout<<"Open device"<< fileDevicePath<<endl;
}

void videodevice::viewInfoDevice(){
    //запрос к драйверу устройства на получения информации
    struct v4l2_capability device_params;
    if(ioctl(fd, VIDIOC_QUERYCAP, &device_params)==-1){
      printf ("\"VIDIOC_QUERYCAP\" error %d, %s\n", errno, strerror(errno));
      exit(EXIT_FAILURE);
    }

    //выводи информацию о камере
    printf("driver : %s\n",device_params.driver);
    printf("card : %s\n",device_params.card);
    printf("bus_info : %s\n",device_params.bus_info);
    printf("version : %d.%d.%d\n",
         ((device_params.version >> 16) & 0xFF),
         ((device_params.version >> 8) & 0xFF),
         (device_params.version & 0xFF));
    printf("capabilities: 0x%08x\n", device_params.capabilities);
    printf("device capabilities: 0x%08x\n", device_params.device_caps);

    //format
    struct v4l2_fmtdesc fmtdesc;
    fmtdesc.index=0;
    fmtdesc.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while(ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc)==0){
        cout<<"image format"<<endl;
        cout<<"pixelformat: "<<fmtdesc.description<<endl;
        fmtdesc.index++;
    }
}

void videodevice::setFormatcam(){
    struct v4l2_format fmt;
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = IMAGE_WIDTH;
    fmt.fmt.pix.height      = IMAGE_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV422P;
    //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;

    if(-1==xioctl(fd, VIDIOC_S_FMT, &fmt))
        errno_exit("VIDIOC_S_FMT");
}

void videodevice::closeDevice(){
    if(close(fd)==-1)
        errno_exit("close");
    fd=-1;

    cout<<"Close device "<<fileDevicePath<<endl;
}

void videodevice::getFrame(int iTime){
    setFormatcam();
    initMMAP();

    long int i=1;
    while(true){
        startCapturing();

        string sFile;
        std::ostringstream os;
        os<<i;
        sFile=os.str();
        
        //=std::to_string(i);
        for(;;){
            if(readFrame(sPath+sFile))
               break;
        }
        ++i;
        stopCapturing();

        cout<<"next frame to "<<iTime<<" sec, frame - "<<i<<endl;
        sleep(iTime);
    }

    freeMMAP();
}

void videodevice::setPath(string sPath){
    this->sPath=sPath;
}

void videodevice::initMMAP(){
    //Инициализация буфера
    struct v4l2_requestbuffers req;
    req.count=1;
    req.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory=V4L2_MEMORY_MMAP;

    xioctl(fd, VIDIOC_REQBUFS, &req);

    //выделить память
    devbuffer=(buffer*)calloc(req.count, sizeof(*devbuffer));

    //отоброжаем буфер на память
    struct v4l2_buffer buf;
    buf.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.index=0;

    xioctl(fd, VIDIOC_QUERYBUF, &buf);

    //отоброзить память устройства в оперативную память
    devbuffer->length=buf.length;
    devbuffer->start=mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
    if(devbuffer->start==MAP_FAILED)
        errno_exit("mmap");

    cout<<"Init mmap"<<endl;
}

void videodevice::startCapturing(){
    struct v4l2_buffer buf;
    buf.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory=V4L2_MEMORY_MMAP;
    buf.index=0;

    //буфер в очередь обработки драйвером устройства
    xioctl(fd, VIDIOC_QBUF, &buf);

    //камеру в режим захвата
    enum v4l2_buf_type type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd, VIDIOC_STREAMON, &type);

    cout<<"Start capturing"<<endl;
}

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
    cinfo.in_color_space=JCS_YCbCr;          //цветовая схема

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

void videodevice::stopCapturing(){
    enum v4l2_buf_type type;

    type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd, VIDIOC_STREAMOFF, &type);

    cout<<"stop Capturing"<<endl;
}

void videodevice::freeMMAP(){
    if(munmap(devbuffer->start, devbuffer->length)==-1)
        errno_exit("munmap");

    free(devbuffer);

    cout<<"free mmap"<<endl;
}
