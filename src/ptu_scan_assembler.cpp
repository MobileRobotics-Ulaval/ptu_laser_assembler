#include <queue> 

#include "ros/ros.h"
#include "ros/console.h"

#include "sensor_msgs/LaserScan.h"
#include "sensor_msgs/JointState.h"

#include "tf/transform_listener.h"

#include <Eigen/Dense>
#include "pointmatcher/PointMatcher.h"
#include "pointmatcher/Timer.h"

#include "pointmatcher_ros/point_cloud.h"
#include "pointmatcher_ros/transform.h"
#include "pointmatcher_ros/get_params_from_server.h"
#include "pointmatcher_ros/ros_logger.h"



using namespace std;

class Assembler
{
    typedef PointMatcher<float> PM;
    typedef PM::DataPoints DP;

    ros::NodeHandle& n;
    ros::NodeHandle& pn;

    // Subscribers
    ros::Subscriber scanSub;
    ros::Subscriber ptuSub;
    ros::Subscriber pointCloudSub;

    // Publishers
    ros::Publisher ptuPub;
    ros::Publisher cloudPub;

    // Parameters
    const string odom_frame;
    const string sensor_frame;
    const string scanTopic;
    const bool oscPan;
    const bool oscTilt;
    const float minPan;
    const float maxPan;
    const float minTilt;
    const float maxTilt;
    const float velocityPan;
    const float velocityTilt;
    const float minRange;
    const float maxRange;
    const bool produceCommand;
    const bool produceCloud;
    const bool produceSingleCloud;

    // States
    bool currentPanMax;
    bool currentTiltMax;
    bool firstPointCloud;

    const int panId;
    const int tiltId;

    tf::TransformListener tfListener;

    DP cloud;
    boost::mutex mutexCloud;
    PM::Transformation *transformation;
    ros::Time scanTime;
    std::queue<sensor_msgs::LaserScan> scanQueue;
    ros::Duration durationBuffer;
    const ros::Duration msgDelay;
    ros::Time timeLastScan;

public:
    Assembler(ros::NodeHandle& n, ros::NodeHandle& pn);
    ~Assembler();

private:
    void gotScan(const sensor_msgs::LaserScan& scanMsgIn);
    void gotJoint(const sensor_msgs::JointState& jointMsgIn);
    void gotCloud(const sensor_msgs::PointCloud2& pointCloud);
    sensor_msgs::JointState buildPtuJoint();
    void publishCloud();

};

Assembler::Assembler(ros::NodeHandle& n, ros::NodeHandle& pn):
    n(n),
    pn(n),
    odom_frame(getParam<string>("odom_frame", "/odom")),
    sensor_frame(getParam<string>("sensor_frame", "/laser")),
    scanTopic(getParam<string>("scanTopic", "/scan")),
    oscPan(getParam<bool>("oscPan", true)),
    oscTilt(getParam<bool>("oscTilt", false)),
    minPan(getParam<double>("minPan", -M_PI/2)),
    maxPan(getParam<double>("maxPan", M_PI/2)),
    minTilt(getParam<double>("minTilt", -0.8)),
    maxTilt(getParam<double>("maxTilt", 0.5)),
    velocityPan(getParam<double>("velocityPan", 2.0)),
    velocityTilt(getParam<double>("velocityTilt", 0.5)),
    minRange(getParam<double>("minRange", 0.05)),
    maxRange(getParam<double>("maxRange", 20.0)),
    produceCommand(getParam<bool>("produceCommand", true)),
    produceCloud(getParam<bool>("produceCloud", true)),
    produceSingleCloud(getParam<bool>("produceSingleCloud", false)),
    firstPointCloud(true),
    panId(0),
    tiltId(1),
    tfListener(n, ros::Duration(30)),
    transformation(PM::get().REG(Transformation).create("RigidTransformation")),
    durationBuffer(ros::Duration(0.5)),
    //msgDelay(ros::Duration(0.062))
    msgDelay(ros::Duration(getParam<double>("msgDelay", 0.12)))
{

    if(produceCloud)
    {
        scanSub = n.subscribe(scanTopic, 2, &Assembler::gotScan, this);
    }

    ptuSub = n.subscribe("/ptu/state", 1, &Assembler::gotJoint, this);

    pointCloudSub = n.subscribe("/cloud", 1, &Assembler::gotCloud, this);

    ptuPub = n.advertise<sensor_msgs::JointState>("/ptu/cmd", 2, true);
    cloudPub = n.advertise<sensor_msgs::PointCloud2>("/cloud", 2, true);

    if(produceCommand)
    {
        while (!ptuPub.getNumSubscribers())
        {
            ROS_WARN_STREAM("Waiting 1s for the PTU node. Will try to publish later ");
            ros::Duration(1).sleep();
        }

        sensor_msgs::JointState joint = buildPtuJoint();

        joint.position[panId] = maxPan;
        joint.velocity[panId] = velocityPan;

        ptuPub.publish(joint);

        // init states
        currentPanMax = true;
    }

}

Assembler::~Assembler()
{

}

sensor_msgs::JointState Assembler::buildPtuJoint()
{
    sensor_msgs::JointState joint;

    joint.header.stamp = ros::Time::now();
    joint.name.resize(2);
    joint.position.resize(2);
    joint.velocity.resize(2);
    joint.name[panId] = "pan";
    joint.name[tiltId] = "tilt";
    joint.position[panId] = 0;
    joint.position[tiltId] = 0;
    joint.velocity[panId] = 0;
    joint.velocity[tiltId] = 0;

    return joint;
}


void Assembler::gotScan(const sensor_msgs::LaserScan& scanMsg)
{
    //PointMatcherSupport::timer t;
    scanQueue.push(scanMsg);

    ros::Duration dt = ros::Time::now() - scanQueue.front().header.stamp;

    if(dt > durationBuffer)
    {
        sensor_msgs::LaserScan scanMsgIn = scanQueue.front();
        scanQueue.pop();

        scanMsgIn.header.stamp += msgDelay;

        scanTime = scanMsgIn.header.stamp;

        // Modify range of the msg before conversion
        scanMsgIn.range_min = minRange;
        scanMsgIn.range_max = maxRange;

        try
        {

            if(cloud.features.cols() == 0)
            {
                cloud = PointMatcher_ros::rosMsgToPointMatcherCloud<float>(
                            scanMsgIn, &tfListener, odom_frame, true, true);
            }
            else
            {
                cloud.concatenate(
                            PointMatcher_ros::rosMsgToPointMatcherCloud<float>(
                                scanMsgIn, &tfListener, odom_frame, true, true));
            }

        }
        catch(tf::ExtrapolationException e)
        {
            ROS_WARN_STREAM(e.what());
        }
        catch(PM::DataPoints::InvalidField e)
        {
            ROS_WARN_STREAM(e.what());
        }

        if(timeLastScan != ros::Time(0) && scanTime > timeLastScan)
        {
            publishCloud();
            timeLastScan = ros::Time(0);
        }
    }

    //double runTime = t.elapsed();
    //cout << "Callback took " << runTime << " sec, (" << 1/runTime << " Hz)" << endl;

}

void Assembler::publishCloud()
{
    if(cloud.features.cols() != 0)
    {
        //mutexCloud.lock();

        PM::TransformationParameters TOdomToScanner =
                PointMatcher_ros::transformListenerToEigenMatrix<float>(
                    tfListener,
                    sensor_frame,
                    odom_frame,
                    scanTime
                    );

        // Bring the scan back to local frame (i.e., centered to the scanner)
        cloud = transformation->compute(cloud, TOdomToScanner);

        if(firstPointCloud)
        {
            firstPointCloud = false;
            ROS_INFO_STREAM("skipping first point cloud");
        }
        else
        {
            if(cloudPub.getNumSubscribers()) {
                cloudPub.publish(
                            PointMatcher_ros::pointMatcherCloudToRosMsg<float>(
                                cloud, sensor_frame, scanTime));                
            }

        }

        cloud = DP();
        //mutexCloud.unlock();
    }
}

void Assembler::gotJoint(const sensor_msgs::JointState& jointMsgIn)
{
    //ROS_INFO("Joint receive");

    const double eps = 0.005;

    sensor_msgs::JointState joint = buildPtuJoint();

    //cout << jointMsgIn.position[panId] << endl;
    if(currentPanMax)
    {
        if(jointMsgIn.position[panId] > maxPan - eps)
        {
            if(produceCommand)
            {
                ROS_DEBUG_STREAM("Going toward minPan(" << minPan << ")");
                joint.position[panId] = minPan;
                joint.velocity[panId] = velocityPan;

                ptuPub.publish(joint);
            }

            currentPanMax = false;
            timeLastScan = jointMsgIn.header.stamp;
        }
    }
    else
    {

        if(jointMsgIn.position[panId] < minPan + eps)
        {
            if(produceCommand)
            {
                ROS_DEBUG_STREAM("Going toward maxPan(" << maxPan << ")");
                joint.position[panId] = maxPan;
                joint.velocity[panId] = velocityPan;

                ptuPub.publish(joint);
            }

            currentPanMax = true;
            timeLastScan = jointMsgIn.header.stamp;
        }
    }
}

void Assembler::gotCloud(const sensor_msgs::PointCloud2 &pointCloud)
{
    if(produceSingleCloud) {
        ROS_WARN_STREAM("Single cloud received, closing the publisher node");
        ros::Duration(5).sleep();
        ros::shutdown();
    }
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "ptu_laser_scan_assembler");

    ros::NodeHandle n;
    ros::NodeHandle pn("~");

    Assembler a(n, pn);

    ros::spin();

    return 0;
}
