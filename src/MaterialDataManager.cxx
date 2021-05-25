/*!
 * \file   src/MaterialDataManager.cxx
 * \brief
 * \author Thomas Helfer
 * \date   21/08/2018
 * \copyright (C) Copyright Thomas Helfer 2018.
 * Use, modification and distribution are subject
 * to one of the following licences:
 * - GNU Lesser General Public License (LGPL), Version 3.0. (See accompanying
 *   file LGPL-3.0.txt)
 * - CECILL-C,  Version 1.0 (See accompanying files
 *   CeCILL-C_V1-en.txt and CeCILL-C_V1-fr.txt).
 */

#include <mutex>
#include <thread>
#include "MGIS/Raise.hxx"
#include "MGIS/Behaviour/Behaviour.hxx"
#include "MGIS/Behaviour/MaterialDataManager.hxx"

namespace mgis {

  namespace behaviour {

    BehaviourIntegrationWorkSpace::BehaviourIntegrationWorkSpace(
        const Behaviour& b)
        : error_message(512),
          mps0(getArraySize(b.mps, b.hypothesis)),
          mps1(getArraySize(b.mps, b.hypothesis)),
          esvs0(getArraySize(b.esvs, b.hypothesis)),
          esvs1(getArraySize(b.esvs, b.hypothesis)) {
    }  // end of BehaviourIntegrationWorkSpace

    BehaviourIntegrationWorkSpace::BehaviourIntegrationWorkSpace(
        BehaviourIntegrationWorkSpace&&) = default;
    BehaviourIntegrationWorkSpace::BehaviourIntegrationWorkSpace(
        const BehaviourIntegrationWorkSpace&) = default;
    BehaviourIntegrationWorkSpace& BehaviourIntegrationWorkSpace::operator=(
        BehaviourIntegrationWorkSpace&&) = default;
    BehaviourIntegrationWorkSpace& BehaviourIntegrationWorkSpace::operator=(
        const BehaviourIntegrationWorkSpace&) = default;
  BehaviourIntegrationWorkSpace::~BehaviourIntegrationWorkSpace() = default;

    MaterialDataManager::MaterialDataManager(const Behaviour& behaviour,
                                             const size_type s)
        : s0(behaviour, s),
          s1(behaviour, s),
          n(s),
          K_stride(getTangentOperatorArraySize(behaviour)),
          b(behaviour) {}  // end of MaterialDataManager

    MaterialDataManager::MaterialDataManager(
        const Behaviour& behaviour,
        const size_type s,
        const MaterialDataManagerInitializer& i)
        : s0(behaviour, s, i.s0),
          s1(behaviour, s, i.s1),
          n(s),
          K_stride(getTangentOperatorArraySize(behaviour)),
          b(behaviour) {
      if (!i.K.empty()) {
        this->useExternalArrayOfTangentOperatorBlocks(i.K);
      }
      if (!i.speed_of_sound.empty()) {
        this->useExternalArrayOfSpeedOfSounds(i.speed_of_sound);
      }
    }  // end of MaterialDataManager

    static void allocateArrayWithoutSynchronization(
        mgis::span<real>& v,
        std::vector<mgis::real>& values,
        const mgis::size_type s) {
      if (v.empty()) {
        constexpr const auto zero = real{0};
        values.resize(s, zero);
        v = mgis::span<real>(values);
      }
    }  // end of allocateArrayWithoutSynchronization

    static void allocateArrayWithSynchronization(
        mgis::span<real>& v,
        std::vector<mgis::real>& values,
        const mgis::size_type s) {
      static std::mutex mt;
      std::lock_guard<std::mutex> lock(mt);
      allocateArrayWithoutSynchronization(v, values, s);
    }  // end of allocateArrayWithSynchronization

    void MaterialDataManager::setThreadSafe(const bool bv) {
      this->thread_safe = bv;
    }  // end of setThreadSafe
    void MaterialDataManager::allocateArrayOfTangentOperatorBlocks() {
      if (this->thread_safe) {
        allocateArrayWithSynchronization(this->K, this->K_values,
                                         this->n * this->K_stride);
      } else {
        allocateArrayWithoutSynchronization(this->K, this->K_values,
                                            this->n * this->K_stride);
      }
    }  // end of allocateArrayOfTangentOperatorBlocks

    void MaterialDataManager::releaseArrayOfTangentOperatorBlocks() {
      this->K = mgis::span<real>();
      this->K_values.clear();
    }  // end of releaseArrayOfTangentOperatorBlocks

    void MaterialDataManager::useExternalArrayOfTangentOperatorBlocks(
        mgis::span<real> m) {
      if (m.size() != this->n * this->K_stride) {
        mgis::raise(
            "MaterialDataManager::useExternalArrayOfTangentOperatorBlocks: "
            "the external memory has not been allocated properly");
      }
      this->releaseArrayOfTangentOperatorBlocks();
      this->K = m;
    }  // end of useExternalArrayOfTangentOperatorBlocks
    void MaterialDataManager::allocateArrayOfSpeedOfSounds() {
      if (this->thread_safe) {
        allocateArrayWithSynchronization(this->speed_of_sound,
                                         this->speed_of_sound_values, this->n);
      } else {
        allocateArrayWithoutSynchronization(
            this->speed_of_sound, this->speed_of_sound_values, this->n);
      }
    }  // end of allocateArrayOfSpeedOfSounds

    void MaterialDataManager::releaseArrayOfSpeedOfSounds() {
      this->speed_of_sound = mgis::span<real>();
      this->speed_of_sound_values.clear();
    }  // end of releaseArrayOfSpeedOfSounds

    void MaterialDataManager::useExternalArrayOfSpeedOfSounds(
        mgis::span<real> m) {
      if (m.size() != this->n) {
        mgis::raise(
            "MaterialDataManager::useExternalArrayOfSpeedOfSounds: "
            "the external memory has not been allocated properly");
      }
      this->releaseArrayOfSpeedOfSounds();
      this->speed_of_sound = m;
    }  // end of useExternalArrayOfSpeedOfSounds

    BehaviourIntegrationWorkSpace&
    MaterialDataManager::getBehaviourIntegrationWorkSpace() {
      if (this->thread_safe) {
        static std::mutex mt;
        std::lock_guard<std::mutex> lock(mt);
        const auto id = std::this_thread::get_id();
        auto p = this->iwks.find(id);
        if (p == this->iwks.end()) {
          std::unique_ptr<BehaviourIntegrationWorkSpace> wk(
              new BehaviourIntegrationWorkSpace(b));
          p = this->iwks.insert({id, std::move(wk)}).first;
        }
        return *(p->second);
      }
      if (this->iwk == nullptr) {
        this->iwk = std::unique_ptr<BehaviourIntegrationWorkSpace>(
            new BehaviourIntegrationWorkSpace(b));
      }
      return *(this->iwk);
    }  // end of getBehaviourIntegrationWorkSpace

    void MaterialDataManager::releaseBehaviourIntegrationWorkspaces() {
      this->iwk.reset();
      this->iwks.clear();
    }  // end of releaseBehaviourIntegrationWorkspaces
    MaterialDataManager::~MaterialDataManager() = default;

    void update(MaterialDataManager& m) {
      std::fill(m.K.begin(), m.K.end(), real{0});
      update_values(m.s0, m.s1);
    }  // end of update

    void revert(MaterialDataManager& m) {
      std::fill(m.K.begin(), m.K.end(), real{0});
      update_values(m.s1, m.s0);
    }  // end of update

  }  // end of namespace behaviour

}  // end of namespace mgis
