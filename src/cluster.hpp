/**
 * @file cluster.hpp
 * @author Max Yvon Zimmermann
 *
 * @copyright GNU Lesser General Public License v3.0 (LGPL-3.0-or-later)
 *
 */

#pragma once

#include <labrat/robot/node.hpp>
#include <labrat/robot/manager.hpp>

#include <vector>

namespace labrat::robot {

class Cluster {
public:
  ~Cluster();

  inline const std::string &getName() const {
    return name;
  }

protected:
  Cluster(const std::string &name);

  /**
   * @brief Construct and add a node to the internal network.
   *
   * @tparam T Type of the node to be added.
   * @tparam Args Types of the arguments to be forwarded to the node specific constructor.
   * @param name Name of the node.
   * @param args Arguments to be forwarded to the node specific constructor.
   * @return std::shared_ptr<T> Pointer to the created node.
   */
  template <typename T, typename... Args>
  std::shared_ptr<T> addNode(const std::string &name, Args &&...args) requires std::is_base_of_v<Node, T> {
    std::shared_ptr<T> result = Manager::get()->addNode<T>(name, std::forward<Args>(args)...);
    nodes.emplace_back(result);

    return result;
  }

private:
  std::vector<std::shared_ptr<Node>> nodes;
  const std::string name;
};

}  // namespace labrat::robot
