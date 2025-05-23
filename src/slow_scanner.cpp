#include "../slow_scanner.h"


std::queue<std::shared_ptr<memory_region>> slow_scanner::get_regions(std::pair<void*, void*> range, DWORD protect)
{
	std::queue<std::shared_ptr<memory_region>> regions;

	auto current_address = range.first;

	while (current_address < range.second) {

		MEMORY_BASIC_INFORMATION mbi;

		SIZE_T result = VirtualQueryEx(LongToHandle(_process_handle), current_address, &mbi, sizeof(mbi));
		if (result == 0)
			break;


		if (reinterpret_cast<BYTE*>(mbi.BaseAddress) < range.first)
			mbi.BaseAddress = range.first;

		if (reinterpret_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize > range.second)
			mbi.RegionSize = reinterpret_cast<uint64_t>(range.second) - reinterpret_cast<uint64_t>(mbi.BaseAddress);

		auto current_region = std::make_shared<memory_region>(_file, mbi, _process_handle);


		if (current_region && current_region->has_protection_flags(protect) && current_region->is_commited() && !current_region->is_memmapped())
			regions.push(current_region);

		current_address = reinterpret_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize;
	}

	return regions;
}

std::vector<std::shared_ptr<scan>> slow_scanner::first_scan(const std::pair<void*, void*>& range, DWORD protect, scan_type type, element_type elem_type, uint64_t raw1, std::optional<uint64_t> raw2)
{
	auto cmp = make_comparator(type, elem_type);
	auto regions = get_regions(range, protect);
	std::vector<std::future<std::shared_ptr<scan>>> futures;

	// Launch parallel scans
	while (!regions.empty()) {
		auto region = regions.front();
		
		regions.pop();

		// Create scan with element size
		auto s = std::make_shared<scan>(region, type, element_size(elem_type));

		auto base = s->region()->base();

		futures.push_back(
			_pool->enqueue([s, cmp, raw1, raw2, this]() {



				if (!s->region()->read_memory())
					return s;

				// Unknown value: accept all
				if (s->type() == scan_type::unknown_value) {
					s->set_valid();
					return s;
				}


				// Perform value-based search
				uint64_t count = s->search_value(cmp, raw1, raw2);
				return s;
				})
		);	
		
	}

	// Collect and filter results
	std::vector<std::shared_ptr<scan>> results;

	for (auto& fut : futures) {
		auto s = fut.get();
		if (s->is_valid()) results.push_back(s);
	}

	// Sort by base address
	std::sort(results.begin(), results.end(),
		[](auto& a, auto& b) {
			return a->region()->base() < b->region()->base();
		});
	return results;
}

void slow_scanner::next_scan(const std::pair<void*, void*>& range, DWORD protect, scan_type type, element_type elem_type, std::vector<std::shared_ptr<scan>>& prev_scans, uint64_t raw1, std::optional<uint64_t> raw2)
{
	auto cmp = make_comparator(type, elem_type);
	auto regions = get_regions(range, protect);
	std::vector<std::shared_ptr<scan>> results;
	size_t idx = 0;

	// Walk through each new region vs. previous scans
	while (idx < prev_scans.size() && !regions.empty()) {
		auto region = regions.front();
		auto prev = prev_scans[idx];

		// Compute absolute addresses
		uint64_t r_start = reinterpret_cast<uint64_t>(region->base());
		uint64_t r_end = r_start + region->size();
		uint64_t p_start = reinterpret_cast<uint64_t>(prev->region()->base());
		uint64_t p_end = p_start + prev->region()->size();

		// Skip non-overlapping cases
		if (r_end <= p_start) {
			regions.pop();
			continue;
		}
		if (p_end <= r_start) {
			++idx;
			continue;
		}

		// Overlap detected: consume this region for processing
		regions.pop();
		auto s = std::make_shared<scan>(region, type, element_size(elem_type));

		if (!region->read_memory())
			continue;

		// Compute overlap interval
		uint64_t ov_start = max(r_start, p_start);
		uint64_t ov_end = min(r_end, p_end);

		size_t  overlap_bytes = (ov_end > ov_start)
			? static_cast<size_t>(ov_end - ov_start)
			: 0;

		size_t elem_sz = element_size(elem_type);
		size_t r_off = static_cast<size_t>(ov_start - r_start);
		size_t p_off = static_cast<size_t>(ov_start - p_start);

		// Fetch raw buffers once
		auto new_bytes = region->raw_bytes();
		auto old_bytes = prev->region()->raw_bytes();

		if (prev->type() == scan_type::unknown_value) {
			// Full raw-memory scan: step through every aligned element in overlap
			for (size_t i = 0; i + elem_sz <= overlap_bytes; i += elem_sz) {
				uint64_t old_v = 0, new_v = 0;
				std::memcpy(&old_v, old_bytes.data() + p_off + i, elem_sz);
				std::memcpy(&new_v, new_bytes.data() + r_off + i, elem_sz);

				if (cmp(new_v, old_v, raw2)) {
					s->add_result({ new_v, new_v, (r_off + i) / elem_sz });
					s->set_valid();
				}
			}
		}
		else {
			// Value-based scan: remap each previous result into the new region
			for (auto const& e : prev->results()) {
				// Compute this element's absolute address in the old snapshot
				uint64_t e_addr = p_start + e.element_index * elem_sz;

				// Skip if that element lies outside the overlap window
				if (e_addr < ov_start || (e_addr + elem_sz) > ov_end)
					continue;

				// Compute the corresponding index in the new region
				size_t new_index = static_cast<size_t>((e_addr - r_start) / elem_sz);

				// Copy the value from the new snapshot
				uint64_t v = 0;
				std::memcpy(&v,
					new_bytes.data() + new_index * elem_sz,
					elem_sz);

				if (type == scan_type::unchanged || type == scan_type::changed
					|| type == scan_type::decreased_value || type == scan_type::increased_value)

					raw1 = e.snapshot_value;
				// Compare and record if it meets criteria
				if (cmp(v, raw1, raw2)) {
					s->add_result({ v, v, new_index });
					s->set_valid();
				}
			}
		}

		if (s->is_valid())
			results.push_back(s);
	}
	// Sort by base address
	std::sort(results.begin(), results.end(),
		[](auto& a, auto& b) { return a->region()->base() < b->region()->base(); });

	prev_scans.clear();
	prev_scans = std::move(results);

}