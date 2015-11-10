#include <ros/ros.h>
#include <tf/transform_listener.h>
#include <geometry_msgs/Twist.h>
#include <vector>
#include <fstream>
#include "hCRF.h"
#include <std_msgs/String.h>
#include <sstream>
#include "GraphUtils.h"
using namespace std;



string filenameModel = "model.txt";
string filenameFeatures = "features.txt";
//string filenameOutput = "result.txt";
//string filenameStats = "stats.txt";

class activity_recognition{
public:
  ros::NodeHandle nh_;
  ros::Publisher action_result_pub;
  int frame_length;
  int sk_feature_num;
  int current_frame;  
  vector<float> result;
  vector<vector<float> > visual_result;
  vector<string> label_name;
  int nblabel;
  int counter;
  dMatrix* sk_data;
  DataSequence* data_seq;
  Toolbox* toolbox;
  Model* pModel;
  InferenceEngine* pInfEngine;


  activity_recognition(int frame_len,int sk_num)
    :frame_length(frame_len),sk_feature_num(sk_num),counter(0)
  {
    current_frame = 0;    
    sk_data = new dMatrix(frame_length,sk_feature_num,0);
    data_seq = new DataSequence;
    toolbox = new ToolboxHCRF(3,OPTIMIZER_BFGS,0);
    toolbox->load((char*)filenameModel.c_str(),(char*)filenameFeatures.c_str());
    pModel=toolbox->getModel();
    pInfEngine=toolbox->getInferenceEngine();
    nblabel=pModel->getNumberOfSequenceLabels();
    for(int i=0; i<nblabel; i++)
	{
	  result.push_back(0);
	  vector<float> vec;
	  visual_result.push_back(vec);
	}
    string label[3]={"working","shake hand","drinking"};
    label_name.assign(label, label+3);

    action_result_pub=nh_.advertise<std_msgs::String>("query_result",1);
 }

  ~activity_recognition()
  {
    if(toolbox)
      {delete toolbox;
	toolbox = NULL;
      }
     if(data_seq)
      {
       	delete data_seq;
	data_seq = NULL;
	sk_data = NULL;
      }
   
    
  }

  double v_norm(vector<double> &v)
  {
    double sum=0.0;
    for(int i=0; i<v.size(); i++){
      sum+=v[i]*v[i];
    }
    return sqrt(sum);
  }
  vector<double> v_sub(tf::StampedTransform transform1, tf::StampedTransform transform2)
  {
    vector<double> v;
    v.push_back(transform1.getOrigin().x()-transform2.getOrigin().x());
    v.push_back(transform1.getOrigin().y()-transform2.getOrigin().y());
    v.push_back(transform1.getOrigin().z()-transform2.getOrigin().z());
    return v;
  }  




};



int main(int argc, char** argv)
{
  int sk_feature_num = 24;
  int frame_length = 50;
  ros::init(argc, argv, "pose_descriptor_extractor");
  activity_recognition ar(frame_length,sk_feature_num);
  ros::Rate rate(10.0);
  tf::TransformListener listener;
  tf::StampedTransform transform;
  vector<tf::StampedTransform> transform_v;      


  
  while(ar.nh_.ok())
    {

      try{

      listener.lookupTransform("/openni_depth_frame", "/head_1",ros::Time(0) ,transform);
      transform_v.push_back(transform);
      listener.lookupTransform("/openni_depth_frame", "/neck_1",ros::Time(0) ,transform);
      transform_v.push_back(transform);
      listener.lookupTransform("/openni_depth_frame","/torso_1", ros::Time(0) ,transform);
      transform_v.push_back(transform);

      listener.lookupTransform("/openni_depth_frame","/left_shoulder_1", ros::Time(0) ,transform);
      transform_v.push_back(transform);
      listener.lookupTransform("/openni_depth_frame", "/left_elbow_1",ros::Time(0) ,transform);
      transform_v.push_back(transform);
      listener.lookupTransform("/openni_depth_frame","/left_hand_1", ros::Time(0) ,transform);
      transform_v.push_back(transform);

      listener.lookupTransform("/openni_depth_frame","/right_shoulder_1", ros::Time(0) ,transform);
      transform_v.push_back(transform);
      listener.lookupTransform("/openni_depth_frame","/right_elbow_1", ros::Time(0) ,transform);
      transform_v.push_back(transform);
      listener.lookupTransform("/openni_depth_frame","/right_hand_1", ros::Time(0) ,transform);
      transform_v.push_back(transform);
      
      /*      listener.lookupTransform("/openni_depth_frame","/left_hip_1", ros::Time(0) ,transform);
      transform_v.push_back(transform);
      listener.lookupTransform("/openni_depth_frame","/left_knee_1", ros::Time(0) ,transform);
      transform_v.push_back(transform);
      listener.lookupTransform("/openni_depth_frame","/left_foot_1", ros::Time(0) ,transform);
      transform_v.push_back(transform);

      listener.lookupTransform("/openni_depth_frame","/right_hip_1", ros::Time(0) ,transform);
      transform_v.push_back(transform);
      listener.lookupTransform("/openni_depth_frame","/right_knee_1", ros::Time(0) ,transform);
      transform_v.push_back(transform);
      listener.lookupTransform("/openni_depth_frame","/right_foot_1", ros::Time(0) ,transform);
      transform_v.push_back(transform);*/
      }
      catch (tf::TransformException ex)
        {
          ros::Duration(1.0).sleep();
        }
      if(!transform_v.empty())
	{
	
	  vector<double> d_l = ar.v_sub(transform_v[5],transform_v[4]);
	  vector<double> d_r = ar.v_sub(transform_v[8],transform_v[7]);      
	  double ref_dis=max(ar.v_norm(d_l), ar.v_norm(d_r)); //use the ||left/right hand - elbow|| as reference distance
	  //        cout<<"distance"<<ref_dis<<endl;
	  vector<vector<double> > p(transform_v.size()-1);
	
	  for(int i=0; i<transform_v.size()-1; i++)
	    {

	      vector<double> temp=(ar.v_sub(transform_v[i+1],transform_v[0])); // every joint relative to head
	      for(int j=0; j<temp.size(); j++)
		{
		  p[i].push_back(temp[j]/ref_dis);
		
		  if(ar.current_frame < ar.frame_length-1)
		    ar.sk_data->setValue(i*temp.size()+j,ar.current_frame,p[i][j]); //insert one frame (24 points)
		  if(ar.current_frame == ar.frame_length-1)
		    {
		      ar.sk_data->setValue(i*temp.size()+j,ar.current_frame,p[i][j]);  
		    }

		}
	    }
	  
	  if(ar.current_frame < ar.frame_length-1)
	    ar.current_frame++;
	  if(ar.current_frame == ar.frame_length-1)
	    ar.sk_data->slid_window(frame_length,sk_feature_num);
	    
	  ar.data_seq->setPrecomputedFeatures(ar.sk_data);
	  
	  
	  //cout<<"result "<<ar.toolbox->realtimeLabelOutput(ar.data_seq)<<endl;
	  //int label = ar.toolbox->realtimeLabelOutput(ar.data_seq);
	  stringstream ss;
	  std_msgs::String action_result;
	  //ar.result[label]++;
	 
	  dMatrix* score;
	  int score_sum=0;
	  score = ar.toolbox->realtimeScoreOutput(ar.data_seq);
	  for(int i=0; i<ar.nblabel; i++)
	    {
	      ar.result[i] += score->getValue(i,0);	      
	    }
	  
	  //----------------------------------
	  int lowest_score = ar.result[0];
		  for(int i=1; i<ar.nblabel; i++)
		    {
		      if(ar.result[i] <= lowest_score)
			lowest_score = ar.result[i];		      
		    }
		  for(int i=0; i<ar.nblabel; i++)
		    {
		      ar.result[i] -= lowest_score;
		      score_sum += ar.result[i]; 
		    }
		  for(int i=0; i<ar.nblabel; i++)
		    {
		      ar.result[i] = ar.result[i]/score_sum;
		      if(ar.visual_result[i].size()>=frame_length)
			ar.visual_result[i].erase(ar.visual_result[i].begin());
		      ar.visual_result[i].push_back(ar.result[i]);
		    
		    }
		  
		  IplImage* graph=drawFloatGraph(&ar.visual_result[0][0], ar.visual_result[0].size(),NULL, 0, 1, 400, 200, ar.label_name[0].c_str());
		  if(ar.visual_result[0].size()>=frame_length)
		    {
		      for(int i=1; i<ar.nblabel; i++)
			drawFloatGraph(&ar.visual_result[i][0], ar.visual_result[i].size() ,graph, 0, 1, 400, 200, ar.label_name[i].c_str());
		      showImage(graph,10);
		      setGraphColor(0);
		    }
		      
		  listener.clear();
		  //----------------------------
	  /*if(ar.counter>=3)
		{
		  int lowest_score = ar.result[0];
		  for(int i=1; i<ar.nblabel; i++)
		    {
		      if(ar.result[i] <= lowest_score)
			lowest_score = ar.result[i];		      
		    }
		  for(int i=0; i<ar.nblabel; i++)
		    {
		      ar.result[i] -= lowest_score;
		      score_sum += ar.result[i]; 
		    }
		  for(int i=0; i<ar.nblabel; i++)
		    {
		      ar.result[i] = ar.result[i]/score_sum;
		      if(ar.visual_result[i].size()>=50)
			ar.visual_result[i].erase(ar.visual_result[i].begin());
		      ar.visual_result[i].push_back(ar.result[i]);
		    }
	
		  IplImage* graph=drawFloatGraph(&ar.visual_result[0][0], ar.visual_result[0].size(),NULL, 0, 1, 400,200);
		  for(int i=1; i<ar.nblabel; i++)
		    drawFloatGraph(&ar.visual_result[i][0], ar.visual_result[i].size() ,graph, 0, 1, 600,200);

		  showImage(graph,10);
		  ss<<"working: "<<ar.result[0]<<" shake hand: "<<ar.result[1]<<" drinking: "<<ar.result[2]<<endl;
		  cout<<ss.str()<<endl;
		  ar.counter=0;
		  score_sum=0;
		  action_result.data=ss.str();
		  ar.action_result_pub.publish(action_result); //pub the result to euslisp to excute is enough
		  std::fill(ar.result.begin(), ar.result.end(), 0);//clear the vector without change its init
		  
		  listener.clear();
		  
		}
	   ar.counter++;*/
	  

	}
      /*      vector<float> test;
      for(float i=0.1; i<1; i=i+0.1)
	test.push_back(i);
      IplImage* graph=drawFloatGraph(&test[0], test.size(), NULL, 0, 1, 400,180) ;
      showImage(graph,0);*/
      transform_v.clear();
      rate.sleep();

    }
 return 0;
}


