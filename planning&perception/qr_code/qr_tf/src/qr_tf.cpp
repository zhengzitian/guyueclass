#include "qr_tf/qr_tf.h"

RNG rng(12345);
#define CAMERAMAT "camera_matrix"
#define DISTCOEFF "distortion_coefficients"

ImageConverter::ImageConverter(ros::NodeHandle nh,const string& calibFile,const string& saveFile)
	: it(nh),
	_calibFile(calibFile),
  sampleRead(saveFile.c_str()),
  lineColor(255, 255, 255),
  run_task(true)
{
	 readParameters();  //when it should be used.

    //使用image_transport订阅图像话题“in” 和 发布图像话题“out” /camera/rgb/image_raw
    image_sub=it.subscribe("/image_raw",1,&ImageConverter::imageCb,this);
    image_pub=it.advertise("qr_navig",1);

    task_switch_sub_ = nh.subscribe("/task_switch", 1, &ImageConverter::TaskSwitchCallback, this );

}

void ImageConverter::readParameters()
{
    readCalibPara(_calibFile.c_str());
    //it.param("image_sub", image_sub, string("/usb_cam/image_raw"));      //topic name
}


void ImageConverter::TaskSwitchCallback(const std_msgs::HeaderPtr &task_switch_msg)
{
    if ( task_switch_msg->frame_id == "qr_navig" || task_switch_msg->frame_id.empty() )
    {
        if ( task_switch_msg->seq == 0 )
        {
            run_task = false;
        }
        else
        {
            run_task = true;
        }
    }
}

int ImageConverter::readCalibPara(string filename)
{
    cv::FileStorage fs(filename,cv::FileStorage::READ);
    if(!fs.isOpened())
    {
        LERROR("Invalid calibration filename.");
        return 0;
    }
    fs[CAMERAMAT]>>m_camMat;
    fs[DISTCOEFF]>>m_distCoeff;

    unit_x = m_camMat.at<double>(0, 0); //if calibrated right, the effect will be good.
    unit_y = m_camMat.at<double>(1, 1);

    //cout <<  "dx= " << unit_x << endl << "dy= "<< unit_y<<endl;
}

void ImageConverter::ProcessFrame(cv_bridge::CvImagePtr cv_ptr)
{
	Mat img, undistor_img;
    img = cv_ptr->image;
    if (img.empty())
      LERROR("no image stream read!Please check the camera first.");
    undistor_img = img.clone();
    //矫正畸变
	//undistort(img, undistor_img, m_camMat, m_distCoeff);
    //img coordinate
    lineColor = Scalar(0, 255, 0);
    DrawArrow(undistor_img, Point(img.cols/2, img.rows/2), Point(img.cols/2, img.rows/2 - 200), 25, 30, lineColor, 2, CV_AA);

    QRDecode(undistor_img);
    DMDecode(undistor_img);
}

float ImageConverter::GetAngelOfTwoVector(Point2f &pt1, Point2f &pt2, Point2f &c)
{
    float theta = atan2(pt1.x - c.x, pt1.y - c.y) - atan2(pt2.x - c.x, pt2.y - c.y);
    if (theta > CV_PI)
        theta -= 2 * CV_PI;
    if (theta < -CV_PI)
        theta += 2 * CV_PI;

   // theta = theta * 180.0 / CV_PI;
    return theta;
}


//订阅回调函数
void ImageConverter::imageCb(const sensor_msgs::ImageConstPtr& msg)
{
    cv_bridge::CvImagePtr cv_ptr;
    try
    {
        //将ROS图像消息转化为适合Opencv的CvImage
        cv_ptr=cv_bridge::toCvCopy(msg,sensor_msgs::image_encodings::BGR8);

    }
    catch(cv_bridge::Exception& e)
    {
        //ROS_ERROR("cv_bridge exception: %s",e.what());
        LERROR("cv_bridge exception: %s",e.what());
        return;
    }
    
    if(run_task)
      ProcessFrame(cv_ptr);
    // printf("OK1\n");
    image_pub.publish(cv_ptr->toImageMsg());
}


void ImageConverter::DrawArrow(cv::Mat& img, cv::Point pStart, cv::Point pEnd, int len, int alpha,
     cv::Scalar& color, int thickness, int lineType)
{
    const double PI = 3.1415926;
    Point arrow;
    //计算 θ 角（最简单的一种情况在下面图示中已经展示，关键在于 atan2 函数，详情见下面）
    double angle = atan2((double)(pStart.y - pEnd.y), (double)(pStart.x - pEnd.x));
    line(img, pStart, pEnd, color, thickness, lineType);
    //计算箭角边的另一端的端点位置（上面的还是下面的要看箭头的指向，也就是pStart和pEnd的位置）
    arrow.x = pEnd.x + len * cos(angle + PI * alpha / 180);
    arrow.y = pEnd.y + len * sin(angle + PI * alpha / 180);
    line(img, pEnd, arrow, color, thickness, lineType);
    arrow.x = pEnd.x + len * cos(angle - PI * alpha / 180);
    arrow.y = pEnd.y + len * sin(angle - PI * alpha / 180);
    line(img, pEnd, arrow, color, thickness, lineType);
}

void ImageConverter::QRDecode(Mat img)
{
    Mat frame, img_gray;
    //Define a scanner
    ImageScanner scanner;
    scanner.set_config(ZBAR_NONE, ZBAR_CFG_ENABLE, 1);

    frame = img.clone();
    cvtColor(img, img_gray, CV_BGR2GRAY);

    int width = img_gray.cols;
    int height = img_gray.rows;
    uchar *raw = (uchar*)(img_gray.data);

    //Wrap image data
    Image image(width, height, "Y800", raw, width*height);

    //scan the image for barcodes
    scanner.scan(image);

    if(image.symbol_begin() == image.symbol_end())
        LERROR("Failed to check qr code.Please ensure the image is right!");
    //Extract results
    for(Image::SymbolIterator symbol = image.symbol_begin(); symbol != image.symbol_end(); ++ symbol)
    {
       
      //LINFO("extract results");
      LINFO("type:", symbol->get_type_name());
      LINFO("decoded:", symbol->get_data());

  	  vector<Point> vp;
      vector<Point2f> pointBuf;
  	  int n = symbol->get_location_size();
  	  for (int i = 0; i < n; i++)
  	  {
  	       vp.push_back(Point(symbol->get_location_x(i),symbol->get_location_y(i)));
           pointBuf.push_back(vp[i]);
  	  }

  	  for (vector<Point2f>::iterator it = pointBuf.begin(); it != pointBuf.end(); it++)
  	  {
  	  	 //cout << "Points:" << *it << endl;
           circle(img, *it, 3, Scalar(255, 0, 0), -1, 8);
  	  }

      // Draw location of the symbols found
      if (symbol->get_location_size() == 4)
	  {
            LINFO("qr_code detected successfully.");
    		line(img, Point(symbol->get_location_x(0), symbol->get_location_y(0)), Point(symbol->get_location_x(1), symbol->get_location_y(1)), Scalar(0, 255, 0), 2, 8, 0);
    		line(img, Point(symbol->get_location_x(1), symbol->get_location_y(1)), Point(symbol->get_location_x(2), symbol->get_location_y(2)), Scalar(0, 255, 0), 2, 8, 0);
    		line(img, Point(symbol->get_location_x(2), symbol->get_location_y(2)), Point(symbol->get_location_x(3), symbol->get_location_y(3)), Scalar(0, 255, 0), 2, 8, 0);
    		line(img, Point(symbol->get_location_x(3), symbol->get_location_y(3)), Point(symbol->get_location_x(0), symbol->get_location_y(0)), Scalar(0, 255, 0), 2, 8, 0);
            //获得四个点的坐标
            double x0=symbol->get_location_x(0);
            double y0=symbol->get_location_y(0);
            double x1=symbol->get_location_x(1);
            double y1=symbol->get_location_y(1);
            double x2=symbol->get_location_x(2);
            double y2=symbol->get_location_y(2);
            double x3=symbol->get_location_x(3);
            double y3=symbol->get_location_y(3);

            //两条对角线的系数和偏移
            double k1=(y2-y0)/(x2-x0);
            double b1=(x2*y0-x0*y2)/(x2-x0);
            double k2=(y3-y1)/(x3-x1);
            double b2=(x3*y1-x1*y3)/(x3-x1);
            //两条对角线交点的X坐标
            double crossX = -(b1-b2)/(k1-k2);
            double crossY = (k1*b2 - k2 *b1)/(k1-k2);

            double centerX = (x0 + x3)/2;
            double centerY = (y0 + y3)/2;

            //qr coordinate
            lineColor = Scalar(0, 0, 255);
            DrawArrow(img, Point(crossX, crossY), Point(centerX, centerY), 25, 30, lineColor, 2, CV_AA);
            DrawArrow(img, Point(crossX, crossY), Point(crossX, crossY- 200), 25, 30, lineColor, 2, CV_AA);
            //L
            lineColor = Scalar(255, 0, 0);
            DrawArrow(img, Point(crossX, crossY), Point(img.cols/2, img.rows/2), 25, 30, lineColor, 2, CV_AA);
            double L = sqrt(pow(crossX - img.cols/2, 2) + pow(crossY - img.rows/2, 2));
            //cout << "length = " << L << endl;
            //caluate the angle
            Point2f c(crossX, crossY);
            Point2f pt1(centerX, centerY);
            Point2f pt2(img.cols/2, img.rows/2);
            Point2f pt3(crossX, crossY- 200);

            float a1 = GetAngelOfTwoVector(pt1, pt2, c);
            float a2 = CV_PI - a1;
            float a3 = GetAngelOfTwoVector(pt1, pt3, c);

            double x = L * cos(a2);
            double y = L * sin(a2);

            double qr_tf_x = x / unit_x;
            double qr_tf_y = y / unit_y;
            double qr_tf_angle = a3;
            double rot = a3 * 180 /CV_PI;

            LINFO("Horizontal Proj:", qr_tf_x);
            LINFO("Vertical Proj:", qr_tf_y);
            LINFO("Angle:", qr_tf_angle );
            //cout << "Rotation:" << rot << endl<< endl;

            sampleRead<< qr_tf_x <<" "<< qr_tf_y <<" "<< qr_tf_angle <<endl;
            //broadcast tf between qr-cam
            static tf::TransformBroadcaster br;
            tf::Transform transform;
            transform.setOrigin( tf::Vector3(-qr_tf_y+0.7, -qr_tf_x, 0.0) );
            tf::Quaternion q;
            q.setRPY(0, 0, qr_tf_angle);
            transform.setRotation(q);
            br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "base_camera", "base_qr"));
	    }

    }
    //imshow("final", frame);
    imshow("captured", img);
    waitKey(1);

    image.set_data(NULL, 0);
}

void ImageConverter::DMDecode(Mat frame)
{
    DmtxImage *img;
    DmtxDecode *dec;
    DmtxRegion *reg;
    DmtxMessage *msg;
    DmtxTime time;
    string result;

    img = dmtxImageCreate(frame.data, frame.cols, frame.rows, DmtxPack24bppRGB);
    dec = dmtxDecodeCreate(img, 1);
    time = dmtxTimeAdd(dmtxTimeNow(), 10);
    reg = dmtxRegionFindNext(dec, &time);

    if(reg != NULL)
    {
        msg = dmtxDecodeMatrixRegion(dec, reg, -1);

        if(msg != NULL)
        {
            result = string(reinterpret_cast<char*>(msg->output));
			cout << result << endl;
            dmtxMessageDestroy(&msg);
        }

        dmtxRegionDestroy(&reg);
    }

    dmtxDecodeDestroy(&dec);
    dmtxImageDestroy(&img);

    LINFO("result:", result);
}
