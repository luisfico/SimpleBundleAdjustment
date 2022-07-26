#include <iostream>
#include <vector>
#include <string>
#include <utility>

// OpenCV
#include <opencv2/opencv.hpp>

#include "misc.hpp"
#include "BundleAdjustment2Viewes.hpp"

using namespace std;

int main(int argc, char* argv[]) {
  if(argc != 3) {
    cout <<
      "usage: this.out [/path/to/image1] [path/to/image2] "
         << endl;
    return -1;
  }

  cv::Mat image1 = cv::imread(argv[1],1);
  cv::Mat image2 = cv::imread(argv[2],1);

  // Camera intristic parameter matrix
  // I did not calibration
/* Default calib
  cv::Mat K = (cv::Mat_<float>(3,3) <<  500.f,   0.f, image1.cols / 2.f,
                                          0.f, 500.f, image1.rows / 2.f,
                                          0.f,   0.f,               1.f);
*/
//calib rs REALSENSE_D435i  1280x720   CameraIntrinsics{1280, 720, 931.741, 930.044, 650.74, 354.855}; // by default RS calib intrinsic  1280x720 without distortion
  cv::Mat K = (cv::Mat_<float>(3,3) <<  931.741,   0.f, 650.74,
                                          0.f, 930.044, 354.855,
                                          0.f,   0.f,               1.f);

  vector<cv::KeyPoint> kpts_vec1, kpts_vec2;
  cv::Mat desc1, desc2;
  cv::Ptr<cv::AKAZE> akaze = cv::AKAZE::create();

  // extract feature points and calculate descriptors
  akaze -> detectAndCompute(image1, cv::noArray(), kpts_vec1, desc1);
  akaze -> detectAndCompute(image2, cv::noArray(), kpts_vec2, desc2);


  cv::BFMatcher* matcher = new cv::BFMatcher(cv::NORM_HAMMING, false);
  // cross check flag set to false
  // because i do cross-ratio-test match
  vector< vector<cv::DMatch> > matches_2nn_12, matches_2nn_21;
  matcher->knnMatch( desc1, desc2, matches_2nn_12, 2 );
  matcher->knnMatch( desc2, desc1, matches_2nn_21, 2 );
  const double ratio = 0.8;

  vector<cv::Point2f> selected_points1, selected_points2;

  for(int i = 0; i < matches_2nn_12.size(); i++) { // i is queryIdx
    if( matches_2nn_12[i][0].distance/matches_2nn_12[i][1].distance < ratio
        and
        matches_2nn_21[matches_2nn_12[i][0].trainIdx][0].distance
          / matches_2nn_21[matches_2nn_12[i][0].trainIdx][1].distance < ratio )
    {
      if(matches_2nn_21[matches_2nn_12[i][0].trainIdx][0].trainIdx
            == matches_2nn_12[i][0].queryIdx)
      {
        selected_points1.push_back(kpts_vec1[matches_2nn_12[i][0].queryIdx].pt);
        selected_points2.push_back(
            kpts_vec2[matches_2nn_21[matches_2nn_12[i][0].trainIdx][0].queryIdx].pt
            );
      }
    }
  }

  if(true) {
    cv::Mat src;
    cv::hconcat(image1, image2, src);
    for(int i = 0; i < selected_points1.size(); i++) {
      cv::line( src, selected_points1[i],
                cv::Point2f(selected_points2[i].x + image1.cols, selected_points2[i].y),
                1, 1, 0 );
    }
    //cv::imwrite("out/match-result.png", src);
  }

  cv::Mat Kd;
  K.convertTo(Kd, CV_64F);

  cv::Mat mask; // unsigned char array
  cv::Mat E = cv::findEssentialMat(selected_points1, selected_points2, Kd.at<double>(0,0),
                           // cv::Point2f(0.f, 0.f),
                           cv::Point2d(image1.cols/2., image1.rows/2.),
                           cv::RANSAC, 0.999, 1.0, mask);
  // E is CV_64F not 32F

  vector<cv::Point2f> inlier_match_points1, inlier_match_points2;
  for(int i = 0; i < mask.rows; i++) {
    if(mask.at<unsigned char>(i)){
      inlier_match_points1.push_back(selected_points1[i]);
      inlier_match_points2.push_back(selected_points2[i]);
    }
  }

  if(true) {
    cv::Mat src;
    cv::hconcat(image1, image2, src);
    for(int i = 0; i < inlier_match_points1.size(); i++) {
      cv::line( src, inlier_match_points1[i],
                cv::Point2f(inlier_match_points2[i].x + image1.cols, inlier_match_points2[i].y),
                1, 1, 0 );
    }
    //cv::imwrite("out/inlier_match_points.png", src);
  }

  mask.release();
  cv::Mat R, t;
  cv::recoverPose(E,
                  inlier_match_points1,
                  inlier_match_points2,
                  R, t, Kd.at<double>(0,0),
                  // cv::Point2f(0, 0),
                  cv::Point2d(image1.cols/2., image1.rows/2.),
                  mask);
  // R,t is CV_64F not 32F

  vector<cv::Point2d> triangulation_points1, triangulation_points2;
  for(int i = 0; i < mask.rows; i++) {
    if(mask.at<unsigned char>(i)){
      triangulation_points1.push_back
                   (cv::Point2d((double)inlier_match_points1[i].x,(double)inlier_match_points1[i].y));
      triangulation_points2.push_back
                   (cv::Point2d((double)inlier_match_points2[i].x,(double)inlier_match_points2[i].y));
    }
  }

  if(true) {
    cv::Mat src;
    cv::hconcat(image1, image2, src);
    for(int i = 0; i < triangulation_points1.size(); i++) {
      cv::line( src, triangulation_points1[i],
                cv::Point2f((float)triangulation_points2[i].x + (float)image1.cols,
                            (float)triangulation_points2[i].y),
                1, 1, 0 );
    }
    //cv::imwrite("out/triangulatedPoints.png", src);
  }

  cv::Mat Rt1 = cv::Mat::eye(3, 4, CV_64FC1);
  cv::Mat Rt2 = cv::Mat::eye(3, 4, CV_64FC1);
  R.copyTo(Rt2.rowRange(0,3).colRange(0,3));
  t.copyTo(Rt2.rowRange(0,3).col(3));


  cv::Mat point3d_homo;
  cv::triangulatePoints(Kd * Rt1, Kd * Rt2,
                        triangulation_points1, triangulation_points2,
                        point3d_homo);
  //point3d_homo is 64F
  //available input type is here
  //https://stackoverflow.com/questions/16295551/how-to-correctly-use-cvtriangulatepoints

  cv::Mat mat_point3d = cv::Mat::zeros(3, point3d_homo.cols, CV_64F);

  std::cout << "Debug mat_point3d postTriangulation= "<< std::endl;
  
  for(int i = 0; i < mat_point3d.cols; i++) {
    mat_point3d.at<double>(0,i) = point3d_homo.at<double>(0,i)/point3d_homo.at<double>(3,i);
    mat_point3d.at<double>(1,i) = point3d_homo.at<double>(1,i)/point3d_homo.at<double>(3,i);
    mat_point3d.at<double>(2,i) = point3d_homo.at<double>(2,i)/point3d_homo.at<double>(3,i);
    //Debug point map
    std::cout << mat_point3d.at<double>(0,i)<<";"<<mat_point3d.at<double>(1,i)<<";"<<mat_point3d.at<double>(2,i) << std::endl;
  }




  const BA2Viewes::PoseAndStructure pose_and_structure {
    Kd,
    std::vector< std::pair< cv::Mat, std::vector<cv::Point2d> > > {std::make_pair(Rt1,triangulation_points1),std::make_pair(Rt2,triangulation_points2)},
    mat_point3d
  };

  /* POSE, STRUCTURE, FULL */
  const BA2Viewes::BAMode ba_mode = BA2Viewes::FULL;
  //const BA2Viewes::BAMode ba_mode = BA2Viewes::STRUCTURE;

  BA2Viewes::Optimizer optimizer{pose_and_structure, ba_mode};
  optimizer.SetImagePair(std::make_pair(image1,image2)); //ENABLE FOR DEBUG 
  optimizer.SetVerbose(true);

  if(ba_mode == BA2Viewes::POSE) {
    std::cout << "[MODE] = POSE" << std::endl;

    cv::Mat Rt1_noise, Rt2_noise;
    AddNoiseToPose(Rt1, Rt1_noise);
    AddNoiseToPose(Rt2, Rt2_noise);
    optimizer.SetTargetData(std::vector<cv::Mat>{Rt1_noise, Rt2_noise});

  }
  else if (ba_mode == BA2Viewes::STRUCTURE) {
    std::cout << "[MODE] = STRUCTURE" << std::endl;
    cv::Mat point3d_noise;
    AddNoiseToStructure(mat_point3d, point3d_noise);
    optimizer.SetTargetData(std::vector<cv::Mat>{point3d_noise});
  }
  else if (ba_mode == BA2Viewes::FULL) {
    /* FULL BA : Tiene un radio de convergencia más estrecho que otros, por lo que (?) Es posible que el ruido deba ser lo suficientemente pequeño
      * Además, la primera cámara se fija en el origen y se toma un grado de libertad de la segunda cámara. (Si no es fijo, no convergerá)
      * El proceso anterior está escrito en el material que sale cuando buscas en Google en [Ajuste del paquete Okaya], de lo contrario no convergerá.
     */
    std::cout << "[MODE] = FULL" << std::endl;
    cv::Mat Rt2_noise;
    AddNoiseToPose(Rt2, Rt2_noise);
 
    std::cout << "Debug pose (no BA)= "<< std::endl;
    std::cout << Rt2<< std::endl;
    std::cout << Rt2.at<double>(0,3)<<";"<<Rt2.at<double>(1,3)<<";"<<Rt2.at<double>(2,3) << std::endl;
  
    cv::Mat point3d_noise;
    AddNoiseToStructure(mat_point3d, point3d_noise);

    optimizer.SetTargetData(std::vector<cv::Mat>{point3d_noise, Rt1, Rt2_noise});
  }

  optimizer.Run();
  optimizer.Spin();

/*
    std::cout << "Debug mat_point3d postBA= "<< std::endl;
    for(int i = 0; i < mat_point3d.cols; i++) 
      std::cout << mat_point3d.at<double>(0,i)<<";"<<mat_point3d.at<double>(1,i)<<";"<<mat_point3d.at<double>(2,i) << std::endl;
 */

  return 0;
}
