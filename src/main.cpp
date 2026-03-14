#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <print>
#include <system_error>
#include <string>
#include <string_view>
#include <array>
#include <cstdint>

namespace das {

	HHOOK g_keyboard_hook{ nullptr };

	// Physical state from real, non-injected key events.
	bool g_alt_physical_down = false;
	bool g_space_physical_down = false;

	// The current space press is being forwarded synthetically as a non-alt-modified space press.
	bool g_forwarding_plain_space = false;

	// We synthesized an alt-up for the current translated space press, so swallow the user's later real alt-up.
	bool g_swallow_next_alt_up = false;

	WORD g_alt_scan = 0;
	bool g_alt_extended = false;

	constexpr ULONG_PTR g_injected_tag = 0x1234ABCDull;

	std::string error_code_to_string(const DWORD errc) {
		return std::system_category().message(errc);
	}

	void print_last_error(const std::string_view what) {
		const auto errc = GetLastError();
		std::println("{} failed: {} ({})", what, errc, error_code_to_string(errc));
	}

	constexpr bool is_alt_key(const DWORD vk) {
		return (vk == VK_MENU) || (vk == VK_LMENU) || (vk == VK_RMENU);
	}

	bool is_our_injected_event(const KBDLLHOOKSTRUCT& info) {
		return ((info.flags & LLKHF_INJECTED) != 0) && (info.dwExtraInfo == g_injected_tag);
	}

	WORD space_scan_code() {
		return static_cast<WORD>(MapVirtualKeyW(VK_SPACE, MAPVK_VK_TO_VSC));
	}

	INPUT make_key_input(const WORD scan, const DWORD addl_flags = 0) {
		INPUT in{};
		in.type = INPUT_KEYBOARD;
		in.ki.wVk = 0;
		in.ki.wScan = scan;
		in.ki.dwFlags = KEYEVENTF_SCANCODE | addl_flags;
		in.ki.dwExtraInfo = g_injected_tag;
		return in;
	}

	template<size_t N>
	bool send_inputs(std::array<INPUT, N>& inputs) {
		constexpr UINT num_expected_inputs{ N };
		return SendInput(UINT{ N }, inputs.data(), sizeof(INPUT)) == num_expected_inputs;
	}

	bool send_alt_up_then_space_down() {
		DWORD alt_flags = KEYEVENTF_KEYUP;
		if (g_alt_extended) {
			alt_flags |= KEYEVENTF_EXTENDEDKEY;
		}

		std::array inputs{
			make_key_input(g_alt_scan, alt_flags),
			make_key_input(space_scan_code()),
		};
		return send_inputs(inputs);
	}

	bool send_space_down() {
		std::array inputs{
			make_key_input(space_scan_code()),
		};
		return send_inputs(inputs);
	}

	bool send_space_up() {
		std::array inputs{
			make_key_input(space_scan_code(), KEYEVENTF_KEYUP),
		};
		return send_inputs(inputs);
	}

	LRESULT CALLBACK detour_keyboard_callback(int nCode, WPARAM wParam, LPARAM lParam) {
		if (nCode != HC_ACTION) {
			return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
		}

		const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);

		// Ignore only the synthetic key events injected by this process.
		if (is_our_injected_event(*info)) {
			return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
		}

		const auto down = (wParam == WM_KEYDOWN) || (wParam == WM_SYSKEYDOWN);
		const auto up = (wParam == WM_KEYUP) || (wParam == WM_SYSKEYUP);
		const auto alt = is_alt_key(info->vkCode);
		const auto space = info->vkCode == VK_SPACE;

		if (alt) {
			if (down) {
				g_alt_physical_down = true;
				g_alt_scan = static_cast<WORD>(info->scanCode);
				g_alt_extended = (info->flags & LLKHF_EXTENDED) != 0;

				// If space is currently held, swallow the real alt-down.
				if (g_space_physical_down) {
					return 1;
				}
			}
			else if (up) {
				g_alt_physical_down = false;

				// If we already synthesized alt-up for the current translated space press, swallow the matching real alt-up.
				if (g_swallow_next_alt_up) {
					g_swallow_next_alt_up = false;
					return 1;
				}

				// If space is currently held, also swallow the real alt-up.
				if (g_space_physical_down) {
					return 1;
				}
			}
		}

		if (space) {
			if (down) {
				g_space_physical_down = true;

				// While forwarding synthetic non-alt-modified space input, swallow the real space-down repeat and synthesize space-down instead.
				if (g_forwarding_plain_space) {
					if (!send_space_down()) {
						print_last_error("das::send_space_down");
					}
					return 1;
				}

				const auto alt_active = g_alt_physical_down || ((info->flags & LLKHF_ALTDOWN) != 0);

				// If alt is active, swallow the real space-down and translate it into: synthetic alt-up, then synthetic space-down.
				if (alt_active) {
					if (!send_alt_up_then_space_down()) {
						print_last_error("das::send_alt_up_then_space_down");
						return 1;
					}
					g_forwarding_plain_space = true;
					g_swallow_next_alt_up = true;
					return 1;
				}
			}
			else if (up) {
				g_space_physical_down = false;

				// If the current space press is being forwarded synthetically, swallow the real space-up and synthesize space-up instead.
				if (g_forwarding_plain_space) {
					if (!send_space_up()) {
						print_last_error("das::send_space_up");
					}
					g_forwarding_plain_space = false;
					return 1;
				}
			}
		}

		return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
	}

	bool hook_keyboard_callback() {
		const auto inst = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
		constexpr DWORD all_threads_id = 0;
		g_keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, &detour_keyboard_callback, inst, all_threads_id);
		return g_keyboard_hook != nullptr;
	}

	bool unhook_keyboard_callback() {
		const auto ret = UnhookWindowsHookEx(g_keyboard_hook);
		g_keyboard_hook = nullptr;
		return ret != 0;
	}

} // namespace das

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[], [[maybe_unused]] char* envp[]) {
	std::println("Blocking alt+space / space+alt globally...");

	if (!das::hook_keyboard_callback()) {
		das::print_last_error("das::hook_keyboard_callback");
		return 1;
	}
	else {
		std::println("Success.");
	}

	BOOL ret = false;
	MSG msg{};
	while ((ret = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
		constexpr BOOL err = -1;
		if (ret == err) {
			das::print_last_error("GetMessageW");
			return 1;
		}
		else {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	std::println("Shutting down...");

	if (!das::unhook_keyboard_callback()) {
		das::print_last_error("das::unhook_keyboard_callback");
		return 1;
	}

	return 0;
}