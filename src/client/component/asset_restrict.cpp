#include <std_include.hpp>
#include <loader/component_loader.hpp>
#include "game/game.hpp"

#include <utils/memory.hpp>

namespace asset_restrict
{
	namespace
	{
		void reallocate_asset_pool(game::XAssetType type, const int size)
		{
			const auto asset_size = game::DB_GetXAssetTypeSize(type);
			const auto new_size = size * game::g_poolSize[type];

			const auto new_allocation_size = static_cast<std::size_t>(new_size * asset_size);

			const game::XAssetHeader pool_entry = { .data = utils::memory::allocate(new_allocation_size) };
			game::DB_XAssetPool[type] = pool_entry.data;
			game::g_poolSize[type] = new_size;
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			reallocate_asset_pool(game::ASSET_TYPE_XANIMPARTS, 2);
			reallocate_asset_pool(game::ASSET_TYPE_ATTACHMENT, 2);
		}
	};
}

REGISTER_COMPONENT(asset_restrict::component)
