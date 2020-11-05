#include "coxgraph/server/client_handler.h"

#include <coxgraph_msgs/SubmapsSrv.h>
#include <coxgraph_msgs/TimeLine.h>

#include <string>
#include <vector>

#include "coxgraph/utils/msg_converter.h"

namespace coxgraph {
namespace server {

ClientHandler::Config ClientHandler::getConfigFromRosParam(
    const ros::NodeHandle& nh_private) {
  ClientHandler::Config config;
  nh_private.param<std::string>("client_handler/client_name_prefix",
                                config.client_name_prefix,
                                config.client_name_prefix);
  nh_private.param<std::string>(
      "client_handler/client_loop_closure_topic_suffix",
      config.client_loop_closure_topic_suffix,
      config.client_loop_closure_topic_suffix);
  nh_private.param<std::string>(
      "client_handler/client_map_pose_update_topic_suffix",
      config.client_map_pose_update_topic_suffix,
      config.client_map_pose_update_topic_suffix);
  nh_private.param<int>("client_handler/pub_queue_length",
                        config.pub_queue_length, config.pub_queue_length);
  return config;
}

void ClientHandler::subscribeToTopics() {
  time_line_sub_ = nh_.subscribe(client_node_name_ + "/time_line", 10,
                                 &ClientHandler::timeLineCallback, this);
  sm_pose_updates_sub_ =
      nh_.subscribe(client_node_name_ + "/map_pose_updates", 10,
                    &ClientHandler::submapPoseUpdatesCallback, this);
}

void ClientHandler::timeLineCallback(
    const coxgraph_msgs::TimeLine& time_line_msg) {
  updateTimeLine(time_line_msg.start, time_line_msg.end);
}

void ClientHandler::advertiseTopics() {
  loop_closure_pub_ = nh_.advertise<voxgraph_msgs::LoopClosure>(
      client_node_name_ + "/" + config_.client_loop_closure_topic_suffix,
      config_.pub_queue_length, true);
  sm_pose_tf_pub_ = nh_.advertise<coxgraph_msgs::MapTransform>(
      client_node_name_ + "/" + config_.client_map_pose_update_topic_suffix,
      config_.pub_queue_length, true);
}

void ClientHandler::subscribeToServices() {
  pub_client_submap_client_ = nh_.serviceClient<coxgraph_msgs::ClientSubmapSrv>(
      client_node_name_ + "/get_client_submap");
  get_all_submaps_client_ = nh_.serviceClient<coxgraph_msgs::SubmapsSrv>(
      client_node_name_ + "/get_all_submaps");
}

ClientHandler::ReqState ClientHandler::requestSubmapByTime(
    const ros::Time& timestamp, const SerSmId& ser_sm_id, CliSmId* cli_sm_id,
    CliSm::Ptr* submap, Transformation* T_submap_t) {
  if (!time_line_.hasTime(timestamp)) return ReqState::FUTURE;

  coxgraph_msgs::ClientSubmapSrv cli_submap_srv;
  cli_submap_srv.request.timestamp = timestamp;
  if (pub_client_submap_client_.call(cli_submap_srv)) {
    *cli_sm_id = cli_submap_srv.response.submap.map_header.id;
    // TODO(mikexyl): add check if submap is empty
    *submap = utils::cliSubmapFromMsg(
        ser_sm_id, submap_config_, cli_submap_srv.response, &mission_frame_id_);
    tf::transformMsgToKindr<voxblox::FloatingPoint>(
        cli_submap_srv.response.transform, T_submap_t);
    return ReqState::SUCCESS;
  }
  return ReqState::FAILED;
}

void ClientHandler::submapPoseUpdatesCallback(
    const coxgraph_msgs::MapPoseUpdates& map_pose_updates_msg) {
  LOG(INFO) << log_prefix_ << "Received new pose for "
            << map_pose_updates_msg.submap_id.size() << " submaps.";
  submap_collection_ptr_->getPosesUpdateMutex()->lock();
  {
    for (int i = 0; i < map_pose_updates_msg.submap_id.size(); i++) {
      // TODO(mikexyl): don't need to transform submap?
      SerSmId ser_sm_id = submap_collection_ptr_->getSerSmIdByCliSmId(
          client_id_, map_pose_updates_msg.submap_id[i]);
      CHECK(submap_collection_ptr_->exists(ser_sm_id))
          << "CliSmId " << map_pose_updates_msg.submap_id[i]
          << ", SerSmId: " << ser_sm_id;
      geometry_msgs::Pose submap_pose_msg = map_pose_updates_msg.new_pose[i];
      CliSm::Ptr submap_ptr = submap_collection_ptr_->getSubmapPtr(ser_sm_id);
      TransformationD submap_pose;
      tf::poseMsgToKindr(submap_pose_msg, &submap_pose);
      submap_ptr = submap_collection_ptr_->getSubmapPtr(ser_sm_id);
      submap_ptr->setPose(submap_pose.cast<voxblox::FloatingPoint>());
      submap_collection_ptr_->updateOriPose(
          ser_sm_id, submap_pose.cast<voxblox::FloatingPoint>());
      LOG(INFO) << log_prefix_ << "Updating pose for submap cli id: "
                << map_pose_updates_msg.submap_id[i]
                << " ser id: " << ser_sm_id;
    }
  }
  submap_collection_ptr_->getPosesUpdateMutex()->unlock();
}

bool ClientHandler::requestAllSubmaps(std::vector<CliSmIdPack>* submap_packs,
                                      SerSmId* start_ser_sm_id) {
  CHECK(submap_packs != nullptr);
  submap_packs->clear();
  coxgraph_msgs::SubmapsSrv submap_srv;
  if (get_all_submaps_client_.call(submap_srv)) {
    for (auto const& submap_msg : submap_srv.response.submaps)
      submap_packs->emplace_back(
          utils::cliSubmapFromMsg((*start_ser_sm_id)++, submap_config_,
                                  submap_msg, &mission_frame_id_),
          client_id_, submap_msg.map_header.id);
    return true;
  }
  return false;
}

}  // namespace server
}  // namespace coxgraph
