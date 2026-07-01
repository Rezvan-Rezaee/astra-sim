/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#ifndef __COLLECTIVEIMPL_HH__
#define __COLLECTIVEIMPL_HH__

#include <string>

namespace AstraSim {

// For legacy reasons we are calling this and everywhere below 'Collective Implementation'
// but it really is 'Collective Algorithm'.
enum class CollectiveImplType {
    Ring = 0,
    OneRing,
    Direct,
    OneDirect,
    AllToAll,
    DoubleBinaryTreeLocalAllToAll,
    LocalRingNodeA2AGlobalDBT,
    HierarchicalRing,
    DoubleBinaryTree,
    HalvingDoubling,
    OneHalvingDoubling,
    CustomCollectiveImpl,
};

static const char* collectiveImplTypeToString(CollectiveImplType type)
{
    switch (type) {
        case CollectiveImplType::Ring:
            return "Ring";
        case CollectiveImplType::OneRing:
            return "OneRing";
        case CollectiveImplType::Direct:
            return "Direct";
        case CollectiveImplType::OneDirect:
            return "OneDirect";
        case CollectiveImplType::AllToAll:
            return "AllToAll";
        case CollectiveImplType::DoubleBinaryTreeLocalAllToAll:
            return "DoubleBinaryTreeLocalAllToAll";
        case CollectiveImplType::LocalRingNodeA2AGlobalDBT:
            return "LocalRingNodeA2AGlobalDBT";
        case CollectiveImplType::HierarchicalRing:
            return "HierarchicalRing";
        case CollectiveImplType::DoubleBinaryTree:
            return "DoubleBinaryTree";
        case CollectiveImplType::HalvingDoubling:
            return "HalvingDoubling";
        case CollectiveImplType::OneHalvingDoubling:
            return "OneHalvingDoubling";
        case CollectiveImplType::CustomCollectiveImpl:
            return "CustomCollectiveImpl";
        default:
            return "Unknown";
    }
}

/*
 * CollectiveImpl holds the user's description on how a collective operation is implemented.
 * That implementation is held in the system layer input.
 */
class CollectiveImpl {
  public:
    CollectiveImpl(CollectiveImplType type) {
        this->type = type;
    };
    virtual ~CollectiveImpl() = default;

    CollectiveImplType type;
};

/*
 * DirectCollectiveImpl contains user-specified information about Direct
 * implementation of collective algorithms. We have a separte class for
 * DirectCollectiveImpl, because of the collective window, which is also defined
 * in the system layer input.
 */
class DirectCollectiveImpl : public CollectiveImpl {
  public:
    DirectCollectiveImpl(CollectiveImplType type, int direct_collective_window)
        : CollectiveImpl(type) {
        this->direct_collective_window = direct_collective_window;
    }

    int direct_collective_window;
};

/*
 * CustomCollectiveImpl contains information about non-native, custom collective algorithms.
 * This data structure contains the filename of the Chakra ET which holds the implementation.
 * That filename is provided in the System layer input.
 */
class CustomCollectiveImpl : public CollectiveImpl {
  public:
    CustomCollectiveImpl(CollectiveImplType type, std::string filename)
        : CollectiveImpl(type) {
        this->filename = filename;
    }

    /* The filename of the corresponding Chakra ET file */
    std::string filename;
};

} // namespace AstraSim

#endif /* __COLLECTIVEIMPL_HH */