#ifndef COXGRAPH_SERVER_VISUALIZER_SERVER_VISUALIZER_H_
#define COXGRAPH_SERVER_VISUALIZER_SERVER_VISUALIZER_H_

#include <cblox/mesh/submap_mesher.h>
#include <ros/ros.h>
#include <voxblox/io/mesh_ply.h>
#include <voxblox/mesh/mesh_integrator.h>
#include <voxblox_msgs/MultiMesh.h>
#include <voxblox_ros/mesh_vis.h>
#include <voxblox_ros/ptcloud_vis.h>
#include <voxgraph/tools/visualization/submap_visuals.h>

#include <memory>
#include <string>
#include <vector>

#include "coxgraph/common.h"
#include "coxgraph/server/mesh_collection.h"
#include "coxgraph/server/pose_graph_interface.h"
#include "coxgraph/server/submap_collection.h"

namespace coxgraph {
namespace server {
class ServerVisualizer {
 public:
  struct Config {
    Config()
        : mesh_opacity(1.0),
          submap_mesh_color_mode("lambert_color"),
          combined_mesh_color_mode("normals"),
          publish_submap_meshes_every_n_sec(1.0) {}
    float mesh_opacity;
    std::string submap_mesh_color_mode;
    std::string combined_mesh_color_mode;
    float publish_submap_meshes_every_n_sec;

    friend inline std::ostream& operator<<(std::ostream& s, const Config& v) {
      s << std::endl
        << "ServerVisualizer using Config:" << std::endl
        << std::endl
        << "  Mesh Opacity: " << v.mesh_opacity << std::endl
        << "  Submap Mesh Color Mode: " << v.submap_mesh_color_mode << std::endl
        << "  Combined Mesh Color Mode: " << v.combined_mesh_color_mode
        << std::endl
        << "  Publish Submap Meshes Every: "
        << v.publish_submap_meshes_every_n_sec << " s" << std::endl
        << "-------------------------------------------" << std::endl;
      return (s);
    }
  };

  static Config getConfigFromRosParam(const ros::NodeHandle& nh_private);

  typedef std::shared_ptr<ServerVisualizer> Ptr;

  ServerVisualizer(const ros::NodeHandle& nh, const ros::NodeHandle& nh_private,
                   const CliSmConfig& submap_config,
                   const MeshIntegratorConfig& mesh_config)
      : nh_(nh),
        nh_private_(nh_private),
        config_(getConfigFromRosParam(nh_private)),
        submap_config_(submap_config),
        mesh_config_(mesh_config),
        submap_vis_(submap_config, mesh_config),
        mesh_collection_ptr_(new MeshCollection()) {
    LOG(INFO) << config_;

    setMeshOpacity(config_.mesh_opacity);
    setSubmapMeshColorMode(
        voxblox::getColorModeFromString(config_.submap_mesh_color_mode));
    setCombinedMeshColorMode(
        voxblox::getColorModeFromString(config_.combined_mesh_color_mode));

    advertiseTopics();
  }

  ~ServerVisualizer() = default;

  void setMeshOpacity(float mesh_opacity) {
    submap_vis_.setMeshOpacity(mesh_opacity);
  }
  void setSubmapMeshColorMode(voxblox::ColorMode color_mode) {
    submap_vis_.setSubmapMeshColorMode(color_mode);
  }
  void setCombinedMeshColorMode(voxblox::ColorMode color_mode) {
    submap_vis_.setCombinedMeshColorMode(color_mode);
  }

  void advertiseTopics() {
    combined_mesh_pub_ = nh_private_.advertise<visualization_msgs::Marker>(
        "combined_mesh", 10, true);
    separated_mesh_pub_ = nh_private_.advertise<voxblox_msgs::MultiMesh>(
        "separated_mesh", 10, true);
    if (config_.publish_submap_meshes_every_n_sec > 0)
      submap_mesh_pub_timer_ = nh_private_.createTimer(
          ros::Duration(config_.publish_submap_meshes_every_n_sec),
          &ServerVisualizer::publishSubmapMeshesCallback, this);
  }

  MeshCollection::Ptr getMeshCollectionPtr() { return mesh_collection_ptr_; }

  void addSubmapMesh(CliId cid, CliSmId csid,
                     coxgraph_msgs::MeshWithTrajectory mesh_with_traj) {
    mesh_collection_ptr_->addSubmapMesh(cid, csid, mesh_with_traj);
  }

  /**
   * @brief Get the Final Global Mesh object, other submaps will be added to
   * submap_collection and pose graph to optimize, then removed, so
   * submap_collection and pose graph will not be changed.
   *
   * @param submap_collection_ptr
   * @param pose_graph_interface
   * @param other_submaps
   */
  void getFinalGlobalMesh(const SubmapCollection::Ptr& submap_collection_ptr,
                          const PoseGraphInterface& pose_graph_interface,
                          const std::vector<CliSmIdPack>& other_submaps,
                          const std::string& mission_frame,
                          const ros::Publisher& publisher,
                          const std::string& file_path);

  void getFinalGlobalMesh(const SubmapCollection::Ptr& submap_collection_ptr,
                          const PoseGraphInterface& pose_graph_interface,
                          const std::vector<CliSmIdPack>& other_submaps,
                          const std::string& mission_frame,
                          const std::string& file_path) {
    getFinalGlobalMesh(submap_collection_ptr, pose_graph_interface,
                       other_submaps, mission_frame, combined_mesh_pub_,
                       file_path);
  }

 private:
  ros::NodeHandle nh_;
  ros::NodeHandle nh_private_;

  Config config_;

  CliSmConfig submap_config_;
  MeshIntegratorConfig mesh_config_;

  voxgraph::SubmapVisuals submap_vis_;

  MeshCollection::Ptr mesh_collection_ptr_;
  ros::Timer submap_mesh_pub_timer_;
  ros::Publisher combined_mesh_pub_;
  ros::Publisher separated_mesh_pub_;
  void publishSubmapMeshesCallback(const ros::TimerEvent& event) {
    publishSubmapMeshes();
  }
  void publishSubmapMeshes() {
    for (auto& kv : *mesh_collection_ptr_->getSubmapMeshesPtr()) {
      kv.second.mesh.header.stamp = ros::Time::now();
      kv.second.mesh.mesh.header.stamp = ros::Time::now();
      separated_mesh_pub_.publish(kv.second.mesh);
    }
  }
};  // namespace server

}  // namespace server
}  // namespace coxgraph

#endif  // COXGRAPH_SERVER_VISUALIZER_SERVER_VISUALIZER_H_
