#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>

#include "ppapi/c/pp_errors.h"
#include "ppapi/cpp/completion_callback.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/media_stream_video_track.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/rect.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/video_frame.h"
#include "ppapi/utility/completion_callback_factory.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/ppb.h"
#include "ppapi/cpp/var_dictionary.h"

#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc_c.h"
#include "opencv2/contrib/contrib.hpp"
#include "opencv2/highgui/highgui.hpp"

#include "nacl_io/nacl_io.h"

using namespace std;
using namespace cv;

extern int errno;
stringstream err;

// When compiling natively on Windows, PostMessage can be #define-d to
// something else.
#ifdef PostMessage
#undef PostMessage
#endif

namespace {

// This object is the global object representing this plugin library as long
// as it is loaded.
class MediaStreamVideoModule : public pp::Module {
 public:
  MediaStreamVideoModule() : pp::Module() {}
  virtual ~MediaStreamVideoModule() {}
  
  virtual pp::Instance* CreateInstance(PP_Instance instance);
};

class MediaStreamVideoDemoInstance : public pp::Instance {
 public:
  MediaStreamVideoDemoInstance(PP_Instance instance, pp::Module* module);
  virtual ~MediaStreamVideoDemoInstance();

  // pp::Instance implementation (see PPP_Instance).
  virtual void HandleMessage(const pp::Var& message_data);
 
 private:
  //use for count fps
  struct timeval start;
  struct timeval end;
  
  // GL-related functions.
  void ConfigureTrack();
  
  // MediaStreamVideoTrack callbacks.
  void OnConfigure(int32_t result);
  void OnGetFrame(int32_t result, pp::VideoFrame frame);
  
  //Recognize-related functions
  static void *HandleThread(void* arg);
  void CvtFrame(pp::VideoFrame frame);
  
  pp::MediaStreamVideoTrack video_track_;
  pp::CompletionCallbackFactory<MediaStreamVideoDemoInstance> callback_factory_;
  pthread_t thread;
  
  // MediaStreamVideoTrack attributes:
  PP_VideoFrame_Format attrib_format_;
  int32_t attrib_width_;
  int32_t attrib_height_;
  
  //OpenCV attributes:
  IplImage* frame_;
  IplImage* small_img;
  CascadeClassifier face_cascade;
  pp::Size frame_size_;
  String cascadefile;
};

MediaStreamVideoDemoInstance::MediaStreamVideoDemoInstance(
    PP_Instance instance, pp::Module* module)
    : pp::Instance(instance),
      callback_factory_(this),
      attrib_format_(PP_VIDEOFRAME_FORMAT_BGRA),
      attrib_width_(0),
      attrib_height_(0),
      frame_(NULL) {
  gettimeofday(&start,NULL);
  gettimeofday(&end,NULL);
  nacl_io_init_ppapi(pp::Instance::pp_instance(), pp::Module::Get()->get_browser_interface());
  umount("/");
  errno = 0;
  if (mount("","/","httpfs",0,"")) {
    err << "Unable to mount httpfs! error:" << errno;
    LogToConsole(PP_LOGLEVEL_ERROR, pp::Var(err.str()));
    //this->PostMessage(pp::Var(err.str()));
    assert(false);
  }
  if (pthread_create(&thread, NULL, HandleThread, (void *)this)) {
    err << "Unable to initialize HandleThread! error:" << errno;
    LogToConsole(PP_LOGLEVEL_ERROR, pp::Var(err.str()));
    //this->PostMessage(pp::Var(err.str()));
    assert(false);
  }
  small_img = cvCreateImage(cvSize(320, 240), IPL_DEPTH_8U, 4);
}

MediaStreamVideoDemoInstance::~MediaStreamVideoDemoInstance() {
}

void* MediaStreamVideoDemoInstance::HandleThread(void* arg) {
  MediaStreamVideoDemoInstance *ptr = (MediaStreamVideoDemoInstance *)arg;
  const char *cascadefile = "haarcascade_frontalface_default.xml"; 
  if (!ptr->face_cascade.load(cascadefilename)) {
    LogToConsole(PP_LOGLEVEL_ERROR, pp::Var("Unable to load casscade file!"));
    //ptr->PostMessage(pp::Var(err.str()));
    assert(false);
  }
  return ((void *)0);
}

void MediaStreamVideoDemoInstance::HandleMessage(const pp::Var& var_message) {
  // Ignore the message if it is not a dictionary.
  if (!var_message.is_dictionary()) {
    return;
  }
  pp::VarDictionary var_dictionary_message(var_message);
  pp::Var var_track = var_dictionary_message.Get("track");
  if (!var_track.is_resource()) {
    return;
  }
  pp::Resource resource_track = var_track.AsResource();
  video_track_ = pp::MediaStreamVideoTrack(resource_track);
  ConfigureTrack();
}

void MediaStreamVideoDemoInstance::ConfigureTrack() {
  const int32_t attrib_list[] = {
      PP_MEDIASTREAMVIDEOTRACK_ATTRIB_FORMAT, attrib_format_,
      PP_MEDIASTREAMVIDEOTRACK_ATTRIB_WIDTH, attrib_width_,
      PP_MEDIASTREAMVIDEOTRACK_ATTRIB_HEIGHT, attrib_height_,
      PP_MEDIASTREAMVIDEOTRACK_ATTRIB_NONE
    };
  video_track_.Configure(attrib_list, callback_factory_.NewCallback(
      &MediaStreamVideoDemoInstance::OnConfigure));
}

void MediaStreamVideoDemoInstance::OnConfigure(int32_t result) {
  video_track_.GetFrame(callback_factory_.NewCallbackWithOutput(
      &MediaStreamVideoDemoInstance::OnGetFrame));
}

void MediaStreamVideoDemoInstance::OnGetFrame(
    int32_t result, pp::VideoFrame frame) {
  if (result != PP_OK)
    return;
  CvtFrame(frame);
  video_track_.RecycleFrame(frame);
  video_track_.GetFrame(callback_factory_.NewCallbackWithOutput(
      &MediaStreamVideoDemoInstance::OnGetFrame));
  
}

void MediaStreamVideoDemoInstance::CvtFrame(pp::VideoFrame frame){
  char* data = static_cast<char*>(frame.GetDataBuffer());
  pp::Size size;
  frame.GetSize(&size);
  if (size != frame_size_) {
    frame_size_ = size;
  }
  int32_t width = frame_size_.width();
  int32_t height = frame_size_.height();
  stringstream ss;
  ss.clear();
  
  std::vector<Rect> faces;
  Mat frame_gray;
  if(!frame_)frame_= cvCreateImage(cvSize(width, height), IPL_DEPTH_8U, 4);
  memcpy(frame_->imageData, data, frame_->imageSize);
  cvResize(frame_, small_img, CV_INTER_LINEAR);
  cvtColor( Mat(small_img), frame_gray, CV_BGRA2GRAY );
  equalizeHist( frame_gray, frame_gray );
  //-- Detect faces
  face_cascade.detectMultiScale( frame_gray, faces, 1.1, 2, 0|CV_HAAR_SCALE_IMAGE, Size(30, 30) );
  
  ss << "[";
  for( unsigned int i = 0; i < faces.size(); i++ ) {
    ss << "{" << "x:" << faces[i].x << ",width:" << faces[i].width
       << ",y:" << faces[i].y << ",height:" << faces[i].height << "},\n";
  }
  ss << "]";
  this->PostMessage(pp::Var(ss.str()));
  gettimeofday(&end,NULL);
  double duration = (end.tv_sec-start.tv_sec)*1000000+(end.tv_usec-start.tv_usec);
  fprintf(stderr,"\nfps:%f",1/(duration/1000000));
  gettimeofday(&start,NULL);
}

pp::Instance* MediaStreamVideoModule::CreateInstance(PP_Instance instance) {
  return new MediaStreamVideoDemoInstance(instance, this);
}

}// anonymous namespace

namespace pp {
// Factory function for your specialization of the Module object.
Module* CreateModule() {
  return new MediaStreamVideoModule();
}
}  // namespace pp
