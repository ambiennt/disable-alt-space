#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <print>
#include <system_error>
#include <string>
#include <string_view>

namespace das {

	HHOOK g_keyboard_hook{ nullptr };
	bool g_alt_down = false;
	bool g_space_down = false;

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

	LRESULT CALLBACK detour_keyboard_callback(int nCode, WPARAM wParam, LPARAM lParam) {
		if (nCode == HC_ACTION) {
			const auto* const info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

			const auto down = (wParam == WM_KEYDOWN) || (wParam == WM_SYSKEYDOWN);
			const auto up = (wParam == WM_KEYUP) || (wParam == WM_SYSKEYUP);
			const auto alt = is_alt_key(info->vkCode);
			const auto space = info->vkCode == VK_SPACE;

			if (alt) {
				if (down) {
					g_alt_down = true;

					// Space was already down, so block space then alt.
					if (g_space_down) {
						return 1;
					}
				}
				else if (up) {
					g_alt_down = false;
				}
			}
			else if (space) {
				if (down) {
					g_space_down = true;

					// Alt was already down, so block alt then space.
					if (g_alt_down) {
						return 1;
					}
				}
				else if (up) {
					g_space_down = false;
				}
			}

			// While both are held, swallow repeated events for either key.
			if ((alt || space) && g_alt_down && g_space_down) {
				return 1;
			}
		}

		return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
	}

	bool hook_keyboard_callback() {
		const auto inst = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
		constexpr DWORD all_threads_id = 0;

		g_keyboard_hook = SetWindowsHookExW(
			WH_KEYBOARD_LL,
			detour_keyboard_callback,
			inst,
			all_threads_id);

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