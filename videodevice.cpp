#include <sys/ioctl.h>          //ioctrl()
#include <linux/videodev2.h>    //struct v4l2_capability
#include <unistd.h>             //close()

#include <sys/mman.h>
#include <sstream>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

#include "videodevice.h"

#define IMAGE_WIDTH     1280
#define IMAGE_HEIGHT    720

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

void videodevice::setFormatcam(){
    struct v4l2_format fmt;
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = IMAGE_WIDTH;
    fmt.fmt.pix.height      = IMAGE_HEIGHT;
    //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV422P;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;

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

        string sFile=to_string(i);
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

    //сохраняем в файл JPG
    string rawFile=file_name+string(".jpg");
    FILE *out_file=fopen(rawFile.c_str(), "w");
    fwrite(temp->start, temp->length, 1, out_file);
    fclose(out_file);

    return true;
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
