#include <stdio.h>

#include <qpainter.h>
#include <QPainter>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QMouseEvent>
#include <QPainter>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <geometry_msgs/Twist.h>
#include <QDebug>
#include <QDialog>
#include <QGraphicsItem>
#include <QSpinBox>
#include <QFileDialog>
#include <QUrl>
#include <QMessageBox>
 
#include "calibration.h"

 
namespace rviz_calibration
{
    // 构造函数，初始化变量
    ImageCalibrator::ImageCalibrator( QWidget* parent ):rviz::Panel( parent )
    {

        //ui.pushButton->setObjectName("test");
        // Initialize Form
        ui.setupUi(this);

        ui.spinBox->setRange(1,10);
        ui.spinBox->setSingleStep(1);
        ui.spinBox->setValue(1);
        
        ui.pushButton->setEnabled(false);

        flag_image_video = true;

        ItemModel = new QStandardItemModel(this);

        frame_video = 2 ;
        factor = 1 ;

        UpdateTopicList();

        UpdatePointCloudTopicList();

        w =  new MainWindow(this);

        pub_time = nh.advertise<std_msgs::Header>("time", 10);

        pub_camerainfo = nh.advertise<sensor_msgs::CameraInfo>(camera_info_name, 10);

        pub_projection = nh.advertise<rviz_calibration::projection_matrix>(projection_matrix_name,10);

        pub_point_image = nh.advertise<rviz_calibration::PointsImage>("points_image",10);

        ui.image_topic_comboBox->installEventFilter(this);
        ui.comboBox_PointCloud->installEventFilter(this);

        QObject::connect(this,SIGNAL(selectPoint(QPoint)),this,SLOT(showPoint(QPoint)));
        QObject::connect(ui.image_topic_comboBox,SIGNAL(activated(int)),this,SLOT(image_topic_comboBox_activated(int)));
        QObject::connect(ui.comboBox_PointCloud,SIGNAL(activated(int)),this,SLOT(comboBox_PointCloud_activated(int)));
        QObject::connect(this,SIGNAL(clearPonit()),this,SLOT(clearSelectPonit()));
        QObject::connect(ui.pushButton,SIGNAL(clicked()),this,SLOT(toImageCalibration()));
        QObject::connect(this,SIGNAL(setPixMap(QPixmap)),w,SLOT(setBackGroundPic(QPixmap)));
        QObject::connect(this,SIGNAL(resetPaintWidget()),w,SLOT(changePaintWidget()));
        QObject::connect(ui.pushButton_2,SIGNAL(clicked()),this,SLOT(saveCurrentPixmap()));
        QObject::connect(ui.spinBox,SIGNAL(valueChanged(int)),this,SLOT(setVideoFrame(int)));
        QObject::connect(this,SIGNAL(scale2normal()),w,SLOT(resetViewScale()));
        QObject::connect(ui.StartRectifier,SIGNAL(clicked(bool)),this,SLOT(StartRectifierNode()));
        QObject::connect(ui.openfile,SIGNAL(clicked()),this,SLOT(openFile()));
        QObject::connect(ui.ShowData,SIGNAL(clicked()),this,SLOT(ShowCameraInfo()));
        QObject::connect(ui.publishCameraInfo,SIGNAL(clicked()),this,SLOT(PublishCameraInfo()));
        QObject::connect(ui.convertPointImage,SIGNAL(clicked(bool)),this,SLOT(startConvertPoint2Image(bool)));

        points_image_msg = rviz_calibration::PointsImage::Ptr(new rviz_calibration::PointsImage);
    }
    
    void ImageCalibrator::startConvertPoint2Image(bool checked){
        if(checked == true){
            points_image_sended = true;
            sub_points_no_groud = nh.subscribe<sensor_msgs::PointCloud2>("/points_no_ground",10,&ImageCalibrator::PointCloud2ImageCallback,this);
        }
    }

    void ImageCalibrator::PointCloud2ImageCallback(const sensor_msgs::PointCloud2::ConstPtr& msg){
        rviz_calibration::PointsImage pub_msg = pointcloud2_to_image(msg, CameraExtrinsicMat, CameraMat, DistCoeff, ImageSize);
        *points_image_msg = pub_msg;
        pub_point_image.publish(pub_msg);
    }

    void ImageCalibrator::PublishCameraInfo(){
        sub_header = nh.subscribe<std_msgs::Header>("/time" , 10 , &ImageCalibrator::HeaderCallback , this);

        camera_info_delivery = true;

        ros::AsyncSpinner spinner(2);
        spinner.start();
    }

    void ImageCalibrator::pointsCallback(const sensor_msgs::PointCloud2::ConstPtr& msg)
    {
	    pcl::PointCloud<pcl::PointXYZI> cloud_in;
	    pcl::PointCloud<pcl::PointXYZI> cloud_out;
	    pcl::fromROSMsg(*msg, cloud_in);

	    pcl::PassThrough<pcl::PointXYZI> pass;
	    pass.setInputCloud(cloud_in.makeShared());
	    pass.setFilterFieldName("z");
	    pass.setFilterLimits(-1.8,10.0);
	    pass.filter(cloud_in);

	    pass.setInputCloud(cloud_in.makeShared());
	    pass.setFilterFieldName("x");
	    pass.setFilterLimits(0,17);
	    pass.filter(cloud_out);

	    sensor_msgs::PointCloud2 cloud_msg;
	    pcl::toROSMsg(cloud_out, cloud_msg);
	    cloud_msg.header = msg->header;
	    pub_points.publish(cloud_msg);
    }

    void ImageCalibrator::HeaderCallback(const std_msgs::Header::ConstPtr& header)
    {
        for (int row = 0; row < 3; row++)
		{
			for (int col = 0; col < 3; col++)
			{
				camera_info_msg_.K[row * 3 + col] = CameraMat.at<double>(row, col);
			}
		}

		for (int row = 0; row < 3; row++)
		{
			for (int col = 0; col < 4; col++)
			{
				if (col == 3)
				{
					camera_info_msg_.P[row * 4 + col] = 0.0f;
				} else
				{
					camera_info_msg_.P[row * 4 + col] = CameraMat.at<double>(row, col);
				}
			}
		}

		for (int row = 0; row < DistCoeff.rows; row++)
		{
			for (int col = 0; col < DistCoeff.cols; col++)
			{
				camera_info_msg_.D.push_back(DistCoeff.at<double>(row, col));
			}
		}

        for (int row = 0;row < CameraExtrinsicMat.rows; row ++)
        {
            for (int col = 0; col < CameraExtrinsicMat.cols; col++)
            {
                camera_to_wrold_projection.projection_matrix[row * 4 + col] = CameraExtrinsicMat.at<double>(row,col);
            }
        }

        camera_to_wrold_projection.header = *header;

		camera_info_msg_.distortion_model = DistModel;
		camera_info_msg_.height = ImageSize.height;
		camera_info_msg_.width = ImageSize.width;

        camera_info_msg_.header = *header;
        pub_camerainfo.publish(camera_info_msg_);

        pub_projection.publish(camera_to_wrold_projection);
    }

    void ImageCalibrator::ShowCameraInfo(){
        ReadCameraParameter(parameter_path);

        //ui.listViewCamera->clear();

        QString s_cam_m = Mat2QString(CameraMat);
        QStandardItem *cam_name = new QStandardItem("CameraMat");
        ItemModel->appendRow(cam_name);
        QStandardItem *cam_m = new QStandardItem(s_cam_m);
        ItemModel->appendRow(cam_m);

        QString s_DistCoeff = Mat2QString(DistCoeff);
        QStandardItem *dis_name = new QStandardItem("DistCoeff");
        ItemModel->appendRow(dis_name);
        QStandardItem *distcoeff = new QStandardItem(s_DistCoeff);
        ItemModel->appendRow(distcoeff);

        int height = ImageSize.height;
        int width = ImageSize.width;
        QStandardItem *ImageSize_name = new QStandardItem("ImageSize");
        ItemModel->appendRow(ImageSize_name);
        QString size = "[" + QString::number(height) + "," + QString::number(width) + "]";
        QStandardItem *cam_size = new QStandardItem(size);
        ItemModel->appendRow(cam_size);

        QStandardItem *DistModel_name = new QStandardItem("DistModel");
        ItemModel->appendRow(DistModel_name);
        QString dis = QString::fromStdString(DistModel);
        QStandardItem *dist = new QStandardItem(dis);
        ItemModel->appendRow(dist);

        QStandardItem *CameraType_name = new QStandardItem("CameraType");
        ItemModel->appendRow(CameraType_name);
        QString cameratype_s = QString::fromStdString(CameraType);
        QStandardItem *camera_type = new QStandardItem(cameratype_s);
        ItemModel->appendRow(camera_type);

        ui.listViewCamera->setModel(ItemModel);

        // pointerManagement(cam_name);
        // pointerManagement(cam_m);    
        // pointerManagement(dis_name);
        // pointerManagement(distcoeff);
        // pointerManagement(ImageSize_name);
        // pointerManagement(cam_size);
        // pointerManagement(DistModel_name);
        // pointerManagement(dist);
    
    }

    void ImageCalibrator::initMatrix(const cv::Mat& cameraExtrinsicMat)
    {
        invRt = cameraExtrinsicMat(cv::Rect(0, 0, 3, 3));
        cv::Mat invT = -invRt.t() * (cameraExtrinsicMat(cv::Rect(3, 0, 1, 3)));
        invTt = invT.t();
        init_matrix = true;
    }

    rviz_calibration::PointsImage ImageCalibrator::pointcloud2_to_image(const sensor_msgs::PointCloud2::ConstPtr& pointcloud2, const cv::Mat& cameraExtrinsicMat, const cv::Mat& cameraMat,
        const cv::Mat& distCoeff, const cv::Size& imageSize)
    {
        int w = imageSize.width;
        int h = imageSize.height;

        rviz_calibration::PointsImage msg;

        msg.header = pointcloud2->header;

        msg.intensity.assign(w * h, 0);
        msg.distance.assign(w * h, 0);
        msg.min_height.assign(w * h, 0);
        msg.max_height.assign(w * h, 0);

        uintptr_t cp = (uintptr_t)pointcloud2->data.data();

        msg.max_y = -1;
        msg.min_y = h;

        msg.image_height = imageSize.height;
        msg.image_width = imageSize.width;
        if (!init_matrix)
        {
            initMatrix(cameraExtrinsicMat);
        }

        cv::Mat point(1, 3, CV_64F);
        cv::Point2d imagepoint;
        for (uint32_t y = 0; y < pointcloud2->height; ++y)
        {
            for (uint32_t x = 0; x < pointcloud2->width; ++x)
            {
                float* fp = (float*)(cp + (x + y * pointcloud2->width) * pointcloud2->point_step);

                if (fp[0] <= 1)
                {   
                    continue;
                }

                double intensity = fp[4];
                for (int i = 0; i < 3; i++)
                {
                    point.at<double>(i) = invTt.at<double>(i);
                    for (int j = 0; j < 3; j++)
                    {
                        point.at<double>(i) += double(fp[j]) * invRt.at<double>(j, i);
                    }
                }
            
                double tmpx = point.at<double>(0) / point.at<double>(2);
                double tmpy = point.at<double>(1) / point.at<double>(2);

                if( fabs( atan(tmpx) ) > 0.75 )
                    continue;
                double r2 = tmpx * tmpx + tmpy * tmpy;
                double tmpdist =
                    1 + distCoeff.at<double>(0) * r2 + distCoeff.at<double>(1) * r2 * r2 + distCoeff.at<double>(4) * r2 * r2 * r2;

                imagepoint.x =
                    tmpx * tmpdist + 2 * distCoeff.at<double>(2) * tmpx * tmpy + distCoeff.at<double>(3) * (r2 + 2 * tmpx * tmpx);
                imagepoint.y =
                    tmpy * tmpdist + distCoeff.at<double>(2) * (r2 + 2 * tmpy * tmpy) + 2 * distCoeff.at<double>(3) * tmpx * tmpy;
                imagepoint.x = cameraMat.at<double>(0, 0) * imagepoint.x + cameraMat.at<double>(0, 2);
                imagepoint.y = cameraMat.at<double>(1, 1) * imagepoint.y + cameraMat.at<double>(1, 2);

                int px = int(imagepoint.x + 0.5);
                int py = int(imagepoint.y + 0.5);
                if (0 <= px && px < w && 0 <= py && py < h)
                {
                    int pid = py * w + px;
                    if (msg.distance[pid] == 0 || msg.distance[pid] > point.at<double>(2))
                    {
                        msg.distance[pid] = float(point.at<double>(2) * 100);
                        msg.intensity[pid] = float(intensity);

                        msg.max_y = py > msg.max_y ? py : msg.max_y;
                        msg.min_y = py < msg.min_y ? py : msg.min_y;
                    }
                    if (0 == y && pointcloud2->height == 2)  // process simultaneously min and max during the first layer
                    {
                        float* fp2 = (float*)(cp + (x + (y + 1) * pointcloud2->width) * pointcloud2->point_step);
                        msg.min_height[pid] = fp[2];
                        msg.max_height[pid] = fp2[2];
                    }
                else
                    {
                        msg.min_height[pid] = -1.25;
                        msg.max_height[pid] = 0;
                    }
                }
            }
        }
        return msg;
    }

    void ImageCalibrator::pointerManagement(QStandardItem* p){
        qDebug()<<"1";
        if(p != NULL)
        {
            qDebug()<<"2";
            delete p;
            p = NULL;
        }
    }

    QString ImageCalibrator::Mat2QString(cv::Mat &mat)
    {
        QString s = "";
        for(int i = 0; i<mat.rows;i++)
        {
            for(int j = 0; j<mat.cols;j++)
            s += QString::number(CameraMat.at<double>(i,j),'g',6) + "\t";

            s += "\n";
        }
        return s;
    }

    void ImageCalibrator::ReadCameraParameter(QString parameter_path){
        QString path = parameter_path ;
        std::string path_std = path.toStdString();
        cv::FileStorage fs(path_std, cv::FileStorage::READ);
	    if (!fs.isOpened())
	    {
		    //std::cout << "Cannot open " << argv[1] << std::endl;
		    //return -1;
            qDebug()<<"Cannot open"<<path;
	    }

	    fs["CameraExtrinsicMat"] >> CameraExtrinsicMat;
	    fs["CameraMat"] >> CameraMat;
	    fs["DistCoeff"] >> DistCoeff;
	    fs["ImageSize"] >> ImageSize;
	    fs["DistModel"] >> DistModel;
        fs["CameraType"] >> CameraType;
    }

    void ImageCalibrator::openFile(){
        QFileDialog *fileDialog = new QFileDialog(this);
        //定义文件对话框标题
        fileDialog->setWindowTitle(tr("open yaml"));
        //设置默认文件路径
        fileDialog->setDirectory("/home/nio");
        //设置文件过滤器
        fileDialog->setNameFilter(tr("Images(*.yaml)"));
        //设置可以选择多个文件,默认为只能选择一个文件QFileDialog::ExistingFiles
        fileDialog->setFileMode(QFileDialog::ExistingFiles);
        //设置视图模式
        fileDialog->setViewMode(QFileDialog::Detail);
        //打印所有选择的文件的路径
        QStringList fileNames;
        if(fileDialog->exec())
        {
            fileNames = fileDialog->selectedFiles();
        }
       for(int i = 0;i<fileNames.size();i++){
           parameter_path = fileNames.at(i);
       }
       ui.path->setText(parameter_path);
    }

    void ImageCalibrator::showImage(QPixmap &picture)
    {
        if(points_image_sended == false){
            int height = ui.labelImage->height();
            int width = ui.labelImage->width();
            ui.labelImage->setPixmap(picture.scaled(width,
                                               height,
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
        }
        else{
            points_drawer.Draw(points_image_msg, viewed_image, 3);
            QPixmap view_on_ui_with_points = convert_image::CvMatToQPixmap(viewed_image);
            int height = ui.labelImage->height();
            int width = ui.labelImage->width();
            ui.labelImage->setPixmap(view_on_ui_with_points.scaled(width,
                                               height,
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
        }
    }

    void ImageCalibrator::StartRectifierNode(){
        if (camera_info_delivery == true ){
            image_topic_name_current = ui.image_topic_comboBox->currentText();
            QString image_topic_type_current;

            QMap<QString,QString>::Iterator it_topic;
            it_topic = map_topicName_topicType.find(image_topic_name_current);
            if(it_topic != map_topicName_topicType.end())
                image_topic_type_current = it_topic.value();
            
            image_topic_name_std = image_topic_name_current.toStdString();
            std::string image_topic_type_std = image_topic_type_current.toStdString();
            //sub_image.shutdown();

            image_topic_name_current.push_back("/rectifier");

            if(image_topic_type_std == "sensor_msgs/CompressedImage")
                sub_image_for_rect = nh.subscribe<sensor_msgs::CompressedImage>(image_topic_name_std,1,&ImageCalibrator::DistCorrectionCompressedImageCallback,this);
            else if(image_topic_type_std == "sensor_msgs/Image")
                sub_image_for_rect = nh.subscribe<sensor_msgs::Image>(image_topic_name_std,1,&ImageCalibrator::DistCorrectionImageCallback,this);
        }
        else{
            QMessageBox::warning(this,"error","topic /camera_info has not been published");
        }
    }

    void ImageCalibrator::DistCorrectionCompressedImageCallback(const sensor_msgs::CompressedImage::ConstPtr& msg){
        cv_bridge::CvImagePtr cv_image = cv_bridge::toCvCopy(*msg, "bgr8");
		cv::Mat tmp_image = cv_image->image;
		cv::Mat image;
	
        std::string topic_name_rectifier = image_topic_name_current.toStdString();

		cv::undistort(tmp_image, image, CameraMat, DistCoeff);
		
        pub_image_rected = nh.advertise<sensor_msgs::Image>(topic_name_rectifier,10);

		cv_bridge::CvImage out_msg;
		out_msg.header   = msg->header; // Same timestamp and tf frame as input image
		out_msg.encoding = sensor_msgs::image_encodings::BGR8;
		out_msg.image    = image; // Your cv::Mat

		pub_image_rected.publish(out_msg.toImageMsg());
    }

    void ImageCalibrator::DistCorrectionImageCallback(const sensor_msgs::Image::ConstPtr& msg){
        cv_bridge::CvImagePtr cv_image = cv_bridge::toCvCopy(*msg, "bgr8");
		cv::Mat tmp_image = cv_image->image;
		cv::Mat image;
	
        image_topic_name_current.push_back("/rectifier");
        std::string topic_name_rectifier = image_topic_name_current.toStdString();

		cv::undistort(tmp_image, image, CameraMat, DistCoeff);
		
        pub_image_rected = nh.advertise<sensor_msgs::Image>(topic_name_rectifier,10);

		cv_bridge::CvImage out_msg;
		out_msg.header   = msg->header; // Same timestamp and tf frame as input image
		out_msg.encoding = sensor_msgs::image_encodings::BGR8;
		out_msg.image    = image; // Your cv::Mat

		pub_image_rected.publish(out_msg.toImageMsg());
    }

    void ImageCalibrator::setVideoFrame(int i)
    {
        factor = i;
    }
    void ImageCalibrator::saveCurrentPixmap()
    {
        ui.pushButton->setEnabled(true);
        Q_EMIT setPixMap(view_on_ui);
        Q_EMIT resetPaintWidget();
        Q_EMIT scale2normal();
    }
    void ImageCalibrator::UpdateTopicList(void)
    {
        map_topicName_topicType.clear();

        QStringList image_topic_list;

        QString image_topic_current = ui.image_topic_comboBox->currentText();
        
        if (image_topic_current == "") {
            image_topic_current = kBlankTopic;
        }


        // // Insert blank topic name to the top of the lists
        image_topic_list << kBlankTopic;

        // Get all available topic
        ros::master::V_TopicInfo master_topics;
        ros::master::getTopics(master_topics);

        // Analyse topics
        for (ros::master::V_TopicInfo::iterator it = master_topics.begin(); it != master_topics.end(); it++) {
            const ros::master::TopicInfo &info = *it;
            const QString topic_name = QString::fromStdString(info.name);
            const QString topic_type = QString::fromStdString(info.datatype);

            // Check whether this topic is image
            if ((topic_type.contains(kImageDataType_1) == true) || (topic_type.contains(kImageDataType_2) == true)) {
                image_topic_list << topic_name;
                map_topicName_topicType.insert(topic_name,topic_type);
                continue;
            }

        }

        //remove all list items from combo box
        ui.image_topic_comboBox->clear();

        // set new items to combo box
        ui.image_topic_comboBox->addItems(image_topic_list);
 

        ui.image_topic_comboBox->insertSeparator(1);


        // set last topic as current
        int image_topic_index = ui.image_topic_comboBox->findText(image_topic_current);
     
        if (image_topic_index != -1) {
            ui.image_topic_comboBox->setCurrentIndex(image_topic_index);
        }

    }

    void ImageCalibrator::UpdatePointCloudTopicList(){
        //map_topicName_topicType.clear();

        QStringList points_topic_list;

        QString points_topic_current = ui.image_topic_comboBox->currentText();
        
        if (points_topic_current == "") {
            points_topic_current = kBlankTopic;
        }


        // Insert blank topic name to the top of the lists
        points_topic_list << kBlankTopic;

        // Get all available topic
        ros::master::V_TopicInfo master_topics;
        ros::master::getTopics(master_topics);

        // Analyse topics
        for (ros::master::V_TopicInfo::iterator it = master_topics.begin(); it != master_topics.end(); it++) {
            const ros::master::TopicInfo &info = *it;
            const QString topic_name = QString::fromStdString(info.name);
            const QString topic_type = QString::fromStdString(info.datatype);

            // Check whether this topic is image
            if (topic_type.contains(kPointCloudDataType) == true) {
                points_topic_list << topic_name;
                //map_topicName_topicType.insert(topic_name,topic_type);
                continue;
            }

        }

        //remove all list items from combo box
        ui.comboBox_PointCloud->clear();

        // set new items to combo box
        ui.comboBox_PointCloud->addItems(points_topic_list);
 

        ui.comboBox_PointCloud->insertSeparator(1);


        // set last topic as current
        int points_topic_index = ui.comboBox_PointCloud->findText(points_topic_current);
     
        if (points_topic_index != -1) {
            ui.comboBox_PointCloud->setCurrentIndex(points_topic_index);
        }

    }

    //The event filter to catch clicking on combo box
    bool ImageCalibrator::eventFilter(QObject* object, QEvent* event)
    {
        if (event->type() == QEvent::MouseButtonPress) {
            // combo box will update its contents if this filter is applied
            UpdateTopicList();
            UpdatePointCloudTopicList();
        }

        return QObject::eventFilter(object, event);

    }

    void ImageCalibrator::ImageCallback(const sensor_msgs::Image::ConstPtr& msg)
    {
        std_msgs::Header header_image;
        ros::Time timeStampOfImage;
        std::string frame_id;
        uint seq;
	    timeStampOfImage.sec = msg->header.stamp.sec;
	    timeStampOfImage.nsec = msg->header.stamp.nsec;
        frame_id = msg->header.frame_id;
        seq = msg->header.seq;

        header_image.seq = seq;
        header_image.stamp = timeStampOfImage;
        header_image.frame_id = frame_id;

        pub_time.publish(header_image);

        image_count ++;

        default_count ++;

        //const auto &encoding = sensor_msgs::image_encodings::BGR8;
        viewed_image = cv_bridge::toCvCopy(msg, "bgr8")->image;

        if(image_count % (frame_video * factor ) == 1)
        {
            view_on_ui = convert_image::CvMatToQPixmap(viewed_image);
        }

        //update();
        showImage(view_on_ui);
    }
    void ImageCalibrator::CompressedImageCallback(const sensor_msgs::CompressedImage::ConstPtr& msg)
    {
        std_msgs::Header header_image;
        ros::Time timeStampOfImage;
        std::string frame_id_image;
        uint seq_image;
	    timeStampOfImage.sec = msg->header.stamp.sec;
	    timeStampOfImage.nsec = msg->header.stamp.nsec;
        frame_id_image = msg->header.frame_id;
        seq_image = msg->header.seq;

        header_image.seq = seq_image;
        header_image.stamp = timeStampOfImage;
        header_image.frame_id = frame_id_image;

        pub_time.publish(header_image);

        image_count ++;

        default_count ++;

        //const auto &encoding = sensor_msgs::image_encodings::BGR8;
        viewed_image = cv_bridge::toCvCopy(msg, "bgr8")->image;

        if(image_count % (frame_video * factor ) == 1)
        {
            view_on_ui = convert_image::CvMatToQPixmap(viewed_image);
        }

        //update();
        showImage(view_on_ui);

    }

    void ImageCalibrator::image_topic_comboBox_activated(int index)
    {
        // Extract selected topic name from combo box
        std::string selected_topic = ui.image_topic_comboBox->itemText(index).toStdString();
        QString topic_name = QString::fromStdString(selected_topic);
        QString topic_type;

        if (selected_topic == kBlankTopic.toStdString() || selected_topic == "")
        {
            sub_image.shutdown();

            return;
        }

        QMap<QString,QString>::Iterator it_topic;
        it_topic = map_topicName_topicType.find(topic_name);
        if(it_topic != map_topicName_topicType.end())
            topic_type = it_topic.value();
        // if selected topic is not blank or empty, start callback function
        //default_image_shown_ = false;
        if(topic_type == "sensor_msgs/CompressedImage")
            sub_image = nh.subscribe<sensor_msgs::CompressedImage>(selected_topic , 1 , &ImageCalibrator::CompressedImageCallback , this);
        else if(topic_type == "sensor_msgs/Image")
            sub_image = nh.subscribe<sensor_msgs::Image>(selected_topic , 1 , &ImageCalibrator::ImageCallback , this);
    }

    void ImageCalibrator::comboBox_PointCloud_activated(int index){
        std::string pointcloud_topic_name = ui.comboBox_PointCloud->itemText(index).toStdString();

        if (pointcloud_topic_name == kBlankTopic.toStdString() || pointcloud_topic_name == "")
        {
            sub_points.shutdown();
            return;
        }

        pub_points = nh.advertise<sensor_msgs::PointCloud2>("points_no_ground", 2);

        sub_points = nh.subscribe(pointcloud_topic_name, 2, &ImageCalibrator::pointsCallback,this);
    }

    void ImageCalibrator::showPoint(QPoint p)
    {
        point_count ++ ;
        //ui.textBrowser->insertPlainText("Number of Point " + QString::number(point_count) + '(' + QString::number(p.x()) + ',' + QString::number(p.y()) + ')' + "\n");
    }

    void ImageCalibrator::paintEvent(QPaintEvent *e)
    {
        // if (default_count == 0)
        // {
        //     QString s = "/home/nio/Pictures/xl.jpg";

        //     QPixmap ima_show(s);


        //     QSize q_size= ui.widget->size();
        //     //QSize q_size= ui.graphicsView->size();

        //     ima_show = ima_show.scaled(q_size,Qt::KeepAspectRatio);

        //     QPainter p(this);

        //     p.drawPixmap(ui.widget->pos(), ima_show);
        //     //p.drawPixmap(ui.graphicsView->pos(), ima_show);

        //     p.setPen(QPen(Qt::red, 2, Qt::DashDotLine)); //设置封闭图像的填充颜色,从BrushStyle文件中找，要学会查询函数的使用准则

        //     //Get data from transport buffer
        //     QPoint po;
        //     if(point_trans_buffer.try_pop(po))
        //     {
        //         //Push data into display buffer
        //         point_display_buffer.push(po);
        //     }


        //     //loop the display buffer
        //     point_display_buffer.mut.lock();
        //     for(auto point:point_display_buffer.data_queue)
        //     {
        //         p.drawPoint(point);
        //     }
        //     point_display_buffer.mut.unlock();
        // }
        
        // else
        // {

        //     QPixmap ima_show(view_on_ui);


        //     QSize q_size= ui.widget->size();
        //     //QSize q_size= ui.graphicsView->size();

        //     ima_show = ima_show.scaled(q_size,Qt::KeepAspectRatio);

        //     QPainter p(this);

        //     p.drawPixmap(ui.widget->pos(), ima_show);
        //     //p.drawPixmap(ui.graphicsView->pos(), ima_show);

        //     p.setPen(QPen(Qt::red, 2, Qt::DashDotLine)); //设置封闭图像的填充颜色,从BrushStyle文件中找，要学会查询函数的使用准则

        //     //Get data from transport buffer
        //     QPoint po;
        //     if(point_trans_buffer.try_pop(po))
        //     {
        //     //Push data into display buffer
        //         point_display_buffer.push(po);
        //     }


        // //loop the display buffer
        //     point_display_buffer.mut.lock();
        //     for(auto point:point_display_buffer.data_queue)
        //     {
        //         p.drawPoint(point);
        //     }
        //     point_display_buffer.mut.unlock();

        // }
  

    }

    void ImageCalibrator::mousePressEvent(QMouseEvent *event)
    {
        // QPoint p = ui.widget->pos();
        // int p_x = p.x();
        // int p_y = p.y();
        // int p_w = ui.widget->width();
        // int p_h = ui.widget->height();
        // //QPoint p = ui.graphicsView->pos();
        // //int p_x = p.x();
        // //int p_y = p.y();
        // //int p_w = ui.graphicsView->width();
        // //int p_h = ui.graphicsView->height();

        // //event->button();
        // if(event->button() == Qt::LeftButton)
        // {
        //     QPoint p = event->pos();
        //     if(event->x() >= p_x && event->x() <= (p_x + p_w) && event->y() >= p_y && event->y() <= (p_y+p_h))
        //     {
        //         point_trans_buffer.push(p);
        //     }
        //     //point_trans_buffer.push(p);
        //     Q_EMIT selectPoint(p);

        //     update();
        // }
        // if(event->button() == Qt::RightButton)
        // {
        //     if(event->x() >= p_x && event->x() <= (p_x + p_w) && event->y() >= p_y && event->y() <= (p_y+p_h))
        //     {
        //         point_display_buffer.mut.lock();

        //     //clear the display buffer
        //         point_display_buffer.data_queue.clear();

        //     //loop the display buffer
        //         point_display_buffer.mut.unlock();

        //         point_count = 0;

        //         Q_EMIT clearPonit();

        //         update();
        //     }
        //     //loop the display buffer

        // }
    }

    void ImageCalibrator::clearSelectPonit()
    {
        //ui.textBrowser->clear();
    }

    void ImageCalibrator::toImageCalibration()
    {
        // MainWindow w;
        // QObject::connect(this,SIGNAL(setPixMap(QPixmap)),&w,SLOT(setBackGroundPic(QPixmap)));
        // w.show();
        
        // w =  new MainWindow(this);

        // QObject::connect(this,SIGNAL(setPixMap(QPixmap)),w,SLOT(setBackGroundPic(QPixmap)));
        w->show();
    }

    DrawPoints::DrawPoints(void) {
    // set color map
        cv::Mat gray_scale(256, 1, CV_8UC1);

        for (int i = 0; i < 256; i++) {
        gray_scale.at<uchar>(i) = i;
        }
        cv::applyColorMap(gray_scale, color_map_, cv::COLORMAP_JET);
    } //   DrawPoints::DrawPoints()


    void DrawPoints::Draw(const rviz_calibration::PointsImage::ConstPtr& points, cv::Mat &image, int drawn_size) 
    {
        if (points == NULL) {
            return;
        }

        int width = image.size().width;
        int height = image.size().height;

        // Calculate minimum and maximum value of distance in this points image
        float min_distance, max_distance;
        min_distance = max_distance = points->distance[0];

        for (int i = 1; i < width * height; i++) {
            float distance = points->distance[i];
            max_distance = (distance > max_distance) ? distance : max_distance;
            min_distance = (distance < min_distance) ? distance : min_distance;
        }

        float distance_range = max_distance - min_distance;

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int index = y * width + x;

                float distance = points->distance[index];
                if (distance == 0) {
                    continue;
                }

                // Specify which color will be use for this point
                int color_id = distance_range ? ((distance - min_distance) * 255 / distance_range) : 128;

                // Divide color into each element
                cv::Vec3b color = color_map_.at<cv::Vec3b>(color_id);
                int red   = color[0];
                int green = color[1];
                int blue  = color[2];

                // Draw a point
                int minus_offset = 0;
                int plus_offset = static_cast<int>(drawn_size/2);
                if (drawn_size % 2 == 0) {
                    minus_offset = static_cast<int>(drawn_size/2) - 1;
                } 
                else {
                    minus_offset = static_cast<int>(drawn_size/2);
                }
                cv::rectangle(image,
                      cv::Point(x - minus_offset, y - minus_offset),
                      cv::Point(x + plus_offset, y + plus_offset),
                      CV_RGB(red, green, blue),
                      CV_FILLED);
                }
            }

    } // DrawPoints::Draw()

    GraphicsView::GraphicsView(QWidget *parent) :
    QGraphicsView(parent)
    {
        QObject::connect(this,SIGNAL(updateDraw()),this,SLOT(Draw()));
        scene = new QGraphicsScene;
        scene->setSceneRect(0, 0, 2400, 2300);
    }

    void GraphicsView::mousePressEvent(QMouseEvent *event)
    {
    // 分别获取鼠标点击处在视图、场景和图形项中的坐标，并输出
        QPoint viewPos = event->pos();
        qDebug() << "viewPos: " << viewPos;
        QPointF scenePos = mapToScene(viewPos);
        qDebug() << "scenePos: " << scenePos;

        drawItem->setPos(scenePos);
        Q_EMIT updateDraw();
        // QGraphicsItem *item = scene()->itemAt(scenePos, QTransform());
        // if (item) {
        //     QPointF itemPos = item->mapFromScene(scenePos);
        //     qDebug() << "itemPos: " << itemPos;
        // }
    }

    void GraphicsView::keyPressEvent(QKeyEvent *event)
    {
        switch (event->key()) {
        case Qt::Key_Plus:
            scale(1.2,1.2);
            break;
        case Qt::Key_Minus:
            scale(1/1.2,1/1.2);
            break;
        case Qt::Key_Right:
            rotate(30);
            break;
        }
        QGraphicsView::keyPressEvent(event);
    }

    void GraphicsView::Draw()
    {
        scene->addItem(drawItem);
        this->setScene(scene);
        this->setBackgroundBrush(QPixmap("/home/nio/Pictures/xl.jpg"));
        this->resize(400,300);
    }

} 
 
// 声明此类是一个rviz的插件
#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(rviz_calibration::ImageCalibrator, rviz::Panel )

// END_TUTORIAL

