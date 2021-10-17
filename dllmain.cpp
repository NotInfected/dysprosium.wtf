#include "SDK.h"
#include "Loot.h"
#include "detours.h"
#include "imguipp.h"


typedef int (WINAPI* LPFN_MBA)(DWORD);
static LPFN_MBA NtGetAsyncKeyState;

bool ShowMenu = true;

int Menu_AimBoneInt = 0;
uintptr_t GController = 0;

static const char* AimBone_TypeItems[]{
	"   Head",
	"   Neck",
	"   Pelvis",
	"   Bottom"
};


static const char* ESP_Box_TypeItems[]{
	"   Box",
	"   Cornered",
	"   3D Box"
};

struct APlayerController_FOV_Params
{
	float                                              NewFOV;
};


float color_red = 1.;
float color_green = 0;
float color_blue = 0;
float color_random = 0.0;
float color_speed = -10.0;

void ColorChange()
{
	static float Color[3];
	static DWORD Tickcount = 0;
	static DWORD Tickcheck = 0;
	ImGui::ColorConvertRGBtoHSV(color_red, color_green, color_blue, Color[0], Color[1], Color[2]);
	if (GetTickCount() - Tickcount >= 1)
	{
		if (Tickcheck != Tickcount)
		{
			Color[0] += 0.001f * color_speed;
			Tickcheck = Tickcount;
		}
		Tickcount = GetTickCount();
	}
	if (Color[0] < 0.0f) Color[0] += 1.0f;
	ImGui::ColorConvertHSVtoRGB(Color[0], Color[1], Color[2], color_red, color_green, color_blue);
}

// Speedhack
#pragma comment(lib, "detours.lib")

template<class DataType>
class Monke {
	DataType time_offset;
	DataType time_last_update;

	double speed_;

public:
	Monke(DataType currentRealTime, double initialSpeed) {
		time_offset = currentRealTime;
		time_last_update = currentRealTime;

		speed_ = initialSpeed;
	}

	void setSpeed(DataType currentRealTime, double speed) {
		time_offset = getCurrentTime(currentRealTime);
		time_last_update = currentRealTime;

		speed_ = speed;
	}

	DataType getCurrentTime(DataType currentRealTime) {
		DataType difference = currentRealTime - time_last_update;

		return (DataType)(speed_ * difference) + time_offset;
	}
};

typedef DWORD(WINAPI* GetTickCountType)(void);
typedef ULONGLONG(WINAPI* GetTickCount64Type)(void);

typedef BOOL(WINAPI* QueryPerformanceCounterType)(LARGE_INTEGER* lpPerformanceCount);

GetTickCountType   g_GetTickCountOriginal;
GetTickCount64Type g_GetTickCount64Original;
GetTickCountType   g_TimeGetTimeOriginal;  

QueryPerformanceCounterType g_QueryPerformanceCounterOriginal;


const double kInitialSpeed = 1.0;

Monke<DWORD>     g_Monke(GetTickCount(), kInitialSpeed);
Monke<ULONGLONG> g_MonkeULL(GetTickCount64(), kInitialSpeed);
Monke<LONGLONG>  g_MonkeLL(0, kInitialSpeed);

DWORD     WINAPI GetTickCountHacked(void);
ULONGLONG WINAPI GetTickCount64Hacked(void);

BOOL      WINAPI QueryPerformanceCounterHacked(LARGE_INTEGER* lpPerformanceCount);

DWORD     WINAPI KeysThread(LPVOID lpThreadParameter);

// Dont forget to call this at the start of the cheat or it wont work!

void Gay()
{
	HMODULE kernel32 = GetModuleHandleA(xorstr("Kernel32.dll"));
	HMODULE winmm = GetModuleHandleA(xorstr("Winmm.dll"));

	g_GetTickCountOriginal = (GetTickCountType)GetProcAddress(kernel32, xorstr("GetTickCount"));
	g_GetTickCount64Original = (GetTickCount64Type)GetProcAddress(kernel32, xorstr("GetTickCount64"));

	g_TimeGetTimeOriginal = (GetTickCountType)GetProcAddress(winmm, xorstr("timeGetTime"));

	g_QueryPerformanceCounterOriginal = (QueryPerformanceCounterType)GetProcAddress(kernel32, xorstr("QueryPerformanceCounter"));

	LARGE_INTEGER performanceCounter;
	g_QueryPerformanceCounterOriginal(&performanceCounter);

	g_MonkeLL = Monke<LONGLONG>(performanceCounter.QuadPart, kInitialSpeed);

	DetourTransactionBegin();

	DetourAttach((PVOID*)&g_GetTickCountOriginal, (PVOID)GetTickCountHacked);
	DetourAttach((PVOID*)&g_GetTickCount64Original, (PVOID)GetTickCount64Hacked);

	DetourAttach((PVOID*)&g_TimeGetTimeOriginal, (PVOID)GetTickCountHacked);

	DetourAttach((PVOID*)&g_QueryPerformanceCounterOriginal, (PVOID)QueryPerformanceCounterHacked);

	DetourTransactionCommit();
}

void setAllToSpeed(double speed) {
	g_Monke.setSpeed(g_GetTickCountOriginal(), speed);

	g_MonkeULL.setSpeed(g_GetTickCount64Original(), speed);

	LARGE_INTEGER performanceCounter;
	g_QueryPerformanceCounterOriginal(&performanceCounter);

	g_MonkeLL.setSpeed(performanceCounter.QuadPart, speed);
}

DWORD WINAPI GetTickCountHacked(void) {
	return g_Monke.getCurrentTime(g_GetTickCountOriginal());
}

ULONGLONG WINAPI GetTickCount64Hacked(void) {
	return g_MonkeULL.getCurrentTime(g_GetTickCount64Original());
}

BOOL WINAPI QueryPerformanceCounterHacked(LARGE_INTEGER* lpPerformanceCount) {
	LARGE_INTEGER performanceCounter;

	BOOL result = g_QueryPerformanceCounterOriginal(&performanceCounter);

	lpPerformanceCount->QuadPart = g_MonkeLL.getCurrentTime(performanceCounter.QuadPart);

	return result;
}


PVOID TargetPawn = nullptr;

namespace HookFunctions {
	bool Init(bool NoSpread, bool CalcShot);
	bool NoSpreadInitialized = false;
	bool ShootThroughWallsInitialized = false;
	bool CalcShotInitialized = false;
}


ID3D11Device* device = nullptr;
ID3D11DeviceContext* immediateContext = nullptr;
ID3D11RenderTargetView* renderTargetView = nullptr;
ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;



WNDPROC oWndProc = NULL;


typedef HRESULT(*present_fn)(IDXGISwapChain*, UINT, UINT);
inline present_fn present_original{ };

typedef HRESULT(*resize_fn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
inline resize_fn resize_original{ };


extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void Draw2DBoundingBox(Vector3 StartBoxLoc, float flWidth, float Height, ImColor color)
{
	StartBoxLoc.x = StartBoxLoc.x - (flWidth / 2);
	ImDrawList* Renderer = ImGui::GetOverlayDrawList();
	Renderer->AddLine(ImVec2(StartBoxLoc.x, StartBoxLoc.y), ImVec2(StartBoxLoc.x + flWidth, StartBoxLoc.y), color, 1); //bottom
	Renderer->AddLine(ImVec2(StartBoxLoc.x, StartBoxLoc.y), ImVec2(StartBoxLoc.x, StartBoxLoc.y + Height), color, 1); //left
	Renderer->AddLine(ImVec2(StartBoxLoc.x + flWidth, StartBoxLoc.y), ImVec2(StartBoxLoc.x + flWidth, StartBoxLoc.y + Height), color, 1); //right
	Renderer->AddLine(ImVec2(StartBoxLoc.x, StartBoxLoc.y + Height), ImVec2(StartBoxLoc.x + flWidth, StartBoxLoc.y + Height), color, 1); //up
}

void DrawCorneredBox(int X, int Y, int W, int H, ImColor color, int thickness) {
	float lineW = (W / 3);
	float lineH = (H / 3);
	ImDrawList* Renderer = ImGui::GetOverlayDrawList();
	Renderer->AddLine(ImVec2(X, Y), ImVec2(X, Y + lineH), color, thickness);

	Renderer->AddLine(ImVec2(X, Y), ImVec2(X + lineW, Y), color, thickness);

	Renderer->AddLine(ImVec2(X + W - lineW, Y), ImVec2(X + W, Y), color, thickness);

	Renderer->AddLine(ImVec2(X + W, Y), ImVec2(X + W, Y + lineH), color, thickness);

	Renderer->AddLine(ImVec2(X, Y + H - lineH), ImVec2(X, Y + H), color, thickness);

	Renderer->AddLine(ImVec2(X, Y + H), ImVec2(X + lineW, Y + H), color, thickness);

	Renderer->AddLine(ImVec2(X + W - lineW, Y + H), ImVec2(X + W, Y + H), color, thickness);

	Renderer->AddLine(ImVec2(X + W, Y + H - lineH), ImVec2(X + W, Y + H), color, thickness);

}

bool IsAiming()
{
	return NtGetAsyncKeyState(VK_RBUTTON);
}

auto GetSyscallIndex(std::string ModuleName, std::string SyscallFunctionName, void* Function) -> bool
{
	auto ModuleBaseAddress = LI_FN(GetModuleHandleA)(ModuleName.c_str());
	if (!ModuleBaseAddress)
		ModuleBaseAddress = LI_FN(LoadLibraryA)(ModuleName.c_str());
	if (!ModuleBaseAddress)
		return false;

	auto GetFunctionAddress = LI_FN(GetProcAddress)(ModuleBaseAddress, SyscallFunctionName.c_str());
	if (!GetFunctionAddress)
		return false;

	auto SyscallIndex = *(DWORD*)((PBYTE)GetFunctionAddress + 4);

	*(DWORD*)((PBYTE)Function + 4) = SyscallIndex;

	return true;
}

extern "C"
{
	NTSTATUS _NtUserSendInput(UINT a1, LPINPUT Input, int Size);
};

VOID mouse_event_(DWORD dwFlags, DWORD dx, DWORD dy, DWORD dwData, ULONG_PTR dwExtraInfo)
{
	static bool doneonce;
	if (!doneonce)
	{
		if (!GetSyscallIndex(xorstr("win32u.dll"), xorstr("NtUserSendInput"), _NtUserSendInput))
			return;
		doneonce = true;
	}

	INPUT Input[3] = { 0 };
	Input[0].type = INPUT_MOUSE;
	Input[0].mi.dx = dx;
	Input[0].mi.dy = dy;
	Input[0].mi.mouseData = dwData;
	Input[0].mi.dwFlags = dwFlags;
	Input[0].mi.time = 0;
	Input[0].mi.dwExtraInfo = dwExtraInfo;

	_NtUserSendInput((UINT)1, (LPINPUT)&Input, (INT)sizeof(INPUT));
}


Vector3 head2, neck, pelvis, chest, leftShoulder, rightShoulder, leftElbow, rightElbow, leftHand, rightHand, leftLeg, rightLeg, leftThigh, rightThigh, leftFoot, rightFoot, leftFeet, rightFeet, leftFeetFinger, rightFeetFinger;

bool GetAllBones(uintptr_t CurrentActor) {
	Vector3 chesti, chestatright;

	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 66, &head2);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 65, &neck);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 2, &pelvis);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 36, &chesti);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 8, &chestatright);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 9, &leftShoulder);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 37, &rightShoulder);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 10, &leftElbow);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 38, &rightElbow);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 11, &leftHand);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 39, &rightHand);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 67, &leftLeg);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 74, &rightLeg);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 73, &leftThigh);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 80, &rightThigh);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 68, &leftFoot);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 75, &rightFoot);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 71, &leftFeet);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 78, &rightFeet);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 72, &leftFeetFinger);
	SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 79, &rightFeetFinger);


	SDK::Classes::AController::WorldToScreen(head2, &head2);
	SDK::Classes::AController::WorldToScreen(neck, &neck);
	SDK::Classes::AController::WorldToScreen(pelvis, &pelvis);
	SDK::Classes::AController::WorldToScreen(chesti, &chesti);
	SDK::Classes::AController::WorldToScreen(chestatright, &chestatright);
	SDK::Classes::AController::WorldToScreen(leftShoulder, &leftShoulder);
	SDK::Classes::AController::WorldToScreen(rightShoulder, &rightShoulder);
	SDK::Classes::AController::WorldToScreen(leftElbow, &leftElbow);
	SDK::Classes::AController::WorldToScreen(rightElbow, &rightElbow);
	SDK::Classes::AController::WorldToScreen(leftHand, &leftHand);
	SDK::Classes::AController::WorldToScreen(rightHand, &rightHand);
	SDK::Classes::AController::WorldToScreen(leftLeg, &leftLeg);
	SDK::Classes::AController::WorldToScreen(rightLeg, &rightLeg);
	SDK::Classes::AController::WorldToScreen(leftThigh, &leftThigh);
	SDK::Classes::AController::WorldToScreen(rightThigh, &rightThigh);
	SDK::Classes::AController::WorldToScreen(leftFoot, &leftFoot);
	SDK::Classes::AController::WorldToScreen(rightFoot, &rightFoot);
	SDK::Classes::AController::WorldToScreen(leftFeet, &leftFeet);
	SDK::Classes::AController::WorldToScreen(rightFeet, &rightFeet);
	SDK::Classes::AController::WorldToScreen(leftFeetFinger, &leftFeetFinger);
	SDK::Classes::AController::WorldToScreen(rightFeetFinger, &rightFeetFinger);

	chest.x = chesti.x + ((chestatright.x - chesti.x) / 2);
	chest.y = chesti.y;

	return true;
}

bool InFov(uintptr_t CurrentPawn, int FovValue) {
	Vector3 HeadPos; SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentPawn, 66, &HeadPos); SDK::Classes::AController::WorldToScreen(HeadPos, &HeadPos);
	auto dx = HeadPos.x - (Renderer_Defines::Width / 2);
	auto dy = HeadPos.y - (Renderer_Defines::Height / 2);
	auto dist = sqrtf(dx * dx + dy * dy);

	if (dist < FovValue) {
		return true;
	}
	else {
		return false;
	}
}

bool EntitiyLoop()
{


	ImDrawList* Renderer = ImGui::GetOverlayDrawList();


	if (Settings::crosshair)
	{
		Renderer->AddLine(ImVec2(Renderer_Defines::Width / 2 - 7, Renderer_Defines::Height / 2), ImVec2(Renderer_Defines::Width / 2 + 1, Renderer_Defines::Height / 2), ImColor(255, 0, 0, 255), 1.0);
		Renderer->AddLine(ImVec2(Renderer_Defines::Width / 2 + 8, Renderer_Defines::Height / 2), ImVec2(Renderer_Defines::Width / 2 + 1, Renderer_Defines::Height / 2), ImColor(255, 0, 0, 255), 1.0);
		Renderer->AddLine(ImVec2(Renderer_Defines::Width / 2, Renderer_Defines::Height / 2 - 7), ImVec2(Renderer_Defines::Width / 2, Renderer_Defines::Height / 2), ImColor(255, 0, 0, 255), 1.0);
		Renderer->AddLine(ImVec2(Renderer_Defines::Width / 2, Renderer_Defines::Height / 2 + 8), ImVec2(Renderer_Defines::Width / 2, Renderer_Defines::Height / 2), ImColor(255, 0, 0, 255), 1.0);
	}

	if (Settings::ShowFovCircle and !Settings::fov360)
		Renderer->AddCircle(ImVec2(Renderer_Defines::Width / 2, Renderer_Defines::Height / 2), Settings::FovCircle_Value, SettingsColor::FovCircle, 124);











	try
	{
		float FOVmax = 9999.f;

		float closestDistance = FLT_MAX;
		PVOID closestPawn = NULL;
		bool closestPawnVisible = false;


		uintptr_t MyTeamIndex = 0, EnemyTeamIndex = 0;
		uintptr_t GWorld = read<uintptr_t>(UWorld); if (!GWorld) return false;

		uintptr_t Gameinstance = read<uint64_t>(GWorld + StaticOffsets::OwningGameInstance); if (!Gameinstance) return false;

		uintptr_t LocalPlayers = read<uint64_t>(Gameinstance + StaticOffsets::LocalPlayers); if (!LocalPlayers) return false;

		uintptr_t LocalPlayer = read<uint64_t>(LocalPlayers); if (!LocalPlayer) return false;

		PlayerController = read<uint64_t>(LocalPlayer + StaticOffsets::PlayerController); if (!PlayerController) return false;

		uintptr_t PlayerCameraManager = read<uint64_t>(PlayerController + StaticOffsets::PlayerCameraManager); if (!PlayerCameraManager) return false;

		FOVAngle = SDK::Classes::APlayerCameraManager::GetFOVAngle(PlayerCameraManager);
		SDK::Classes::APlayerCameraManager::GetPlayerViewPoint(PlayerCameraManager, &CamLoc, &CamRot);

		LocalPawn = read<uint64_t>(PlayerController + StaticOffsets::AcknowledgedPawn);

		uintptr_t Ulevel = read<uintptr_t>(GWorld + StaticOffsets::PersistentLevel); if (!Ulevel) return false;

		uintptr_t AActors = read<uintptr_t>(Ulevel + StaticOffsets::AActors); if (!AActors) return false;

		uintptr_t ActorCount = read<int>(Ulevel + StaticOffsets::ActorCount); if (!ActorCount) return false;


		uintptr_t LocalRootComponent;
		Vector3 LocalRelativeLocation;

		if (valid_pointer(LocalPawn)) {
			LocalRootComponent = read<uintptr_t>(LocalPawn + 0x130);
			LocalRelativeLocation = read<Vector3>(LocalRootComponent + 0x11C);
		}

		for (int i = 0; i < ActorCount; i++) {

			auto CurrentActor = read<uintptr_t>(AActors + i * sizeof(uintptr_t));

			auto name = SDK::Classes::UObject::GetObjectName(CurrentActor);

			bool IsVisible = false;

			if (valid_pointer(LocalPawn))
			{


				if (Settings::VehiclesESP && strstr(name, xorstr("Vehicl")) || strstr(name, xorstr("Valet_Taxi")) || strstr(name, xorstr("Valet_BigRig")) || strstr(name, xorstr("Valet_BasicTr")) || strstr(name, xorstr("Valet_SportsC")) || strstr(name, xorstr("Valet_BasicC")))
				{
					uintptr_t ItemRootComponent = read<uintptr_t>(CurrentActor + StaticOffsets::RootComponent);
					Vector3 ItemPosition = read<Vector3>(ItemRootComponent + StaticOffsets::RelativeLocation);
					float ItemDist = LocalRelativeLocation.Distance(ItemPosition) / 100.f;

					if (ItemDist < Settings::MaxESPDistance) {
						Vector3 VehiclePosition;
						SDK::Classes::AController::WorldToScreen(ItemPosition, &VehiclePosition);
						std::string null = xorstr("");
						std::string Text = null + xorstr("Vehicle [") + std::to_string((int)ItemDist) + xorstr("m]");

						ImVec2 TextSize = ImGui::CalcTextSize(Text.c_str());

						Renderer->AddText(ImVec2(VehiclePosition.x, VehiclePosition.y), ImColor(255, 255, 255, 255), Text.c_str());
					}

				}
				else if (Settings::LLamaESP && strstr(name, xorstr("AthenaSupplyDrop_Llama"))) {
					uintptr_t ItemRootComponent = read<uintptr_t>(CurrentActor + StaticOffsets::RootComponent);
					Vector3 ItemPosition = read<Vector3>(ItemRootComponent + StaticOffsets::RelativeLocation);
					float ItemDist = LocalRelativeLocation.Distance(ItemPosition) / 100.f;

					if (ItemDist < Settings::MaxESPDistance) {
						Vector3 LLamaPosition;
						SDK::Classes::AController::WorldToScreen(ItemPosition, &LLamaPosition);

						std::string null = xorstr("");
						std::string Text = null + xorstr("LLama [") + std::to_string((int)ItemDist) + xorstr("m]");

						ImVec2 TextSize = ImGui::CalcTextSize(Text.c_str());

						Renderer->AddText(ImVec2(LLamaPosition.x, LLamaPosition.y), SettingsColor::LLamaESP, Text.c_str());
					}
				}
				else if (strstr(name, xorstr("PlayerPawn"))) {
					Vector3 HeadPos, Headbox, bottom;

					SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 66, &HeadPos);
					SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 0, &bottom);

					SDK::Classes::AController::WorldToScreen(Vector3(HeadPos.x, HeadPos.y, HeadPos.z + 20), &Headbox);
					SDK::Classes::AController::WorldToScreen(bottom, &bottom);

					if (Headbox.x == 0 && Headbox.y == 0) continue;
					if (bottom.x == 0 && bottom.y == 0) continue;






					uintptr_t MyState = read<uintptr_t>(LocalPawn + StaticOffsets::PlayerState);
					if (!MyState) continue;

					MyTeamIndex = read<uintptr_t>(MyState + StaticOffsets::TeamIndex);
					if (!MyTeamIndex) continue;

					uintptr_t EnemyState = read<uintptr_t>(CurrentActor + StaticOffsets::PlayerState);
					if (!EnemyState) continue;

					EnemyTeamIndex = read<uintptr_t>(EnemyState + StaticOffsets::TeamIndex);
					if (!EnemyTeamIndex) continue;

					if (CurrentActor == LocalPawn) continue;

					Vector3 viewPoint;

					if (Settings::VisibleCheck) {
						IsVisible = SDK::Classes::APlayerCameraManager::LineOfSightTo((PVOID)PlayerController, (PVOID)CurrentActor, &viewPoint);
					}


					if (Settings::SnapLines) {
						ImColor col;
						if (IsVisible) {
							col = SettingsColor::Snaplines;
						}
						else {
							col = SettingsColor::Snaplines_notvisible;
						}
						Vector3 LocalPelvis;
						SDK::Classes::USkeletalMeshComponent::GetBoneLocation(LocalPawn, 2, &LocalPelvis);
						SDK::Classes::AController::WorldToScreen(LocalPelvis, &LocalPelvis);

						Renderer->AddLine(ImVec2(LocalPelvis.x, LocalPelvis.y), ImVec2(pelvis.x, pelvis.y), col, 1.f);
					}

					if (Settings::DistanceESP && SDK::Utils::CheckInScreen(CurrentActor, Renderer_Defines::Width, Renderer_Defines::Height)) {
						Vector3 HeadNotW2SForDistance;
						SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 66, &HeadNotW2SForDistance);
						float distance = LocalRelativeLocation.Distance(HeadNotW2SForDistance) / 100.f;

						std::string null = "";
						std::string finalstring = null + xorstr(" [") + std::to_string((int)distance) + xorstr("m]");

						ImVec2 DistanceTextSize = ImGui::CalcTextSize(finalstring.c_str());

						ImColor col;
						if (IsVisible) {
							col = SettingsColor::Distance;
						}
						else {
							col = SettingsColor::Distance_notvisible;
						}

						Renderer->AddText(ImVec2(bottom.x - DistanceTextSize.x / 2, bottom.y + DistanceTextSize.y / 2), col, finalstring.c_str());

					}


					if (Settings::NoSpread) {
						if (!HookFunctions::NoSpreadInitialized) {
							HookFunctions::Init(true, false);
						}
					}



					if (Settings::SilentAim) {
						if (!HookFunctions::CalcShotInitialized) {
							HookFunctions::Init(false, true);
						}

						Vector3 closestPawnviewPoint;
						closestPawnVisible = SDK::Classes::APlayerCameraManager::LineOfSightTo((PVOID)PlayerController, (PVOID)closestPawn, &closestPawnviewPoint);

						if (!Settings::fov360) {
							auto dx = Headbox.x - (Renderer_Defines::Width / 2);
							auto dy = Headbox.y - (Renderer_Defines::Height / 2);
							auto dist = SpoofRuntime::sqrtf_(dx * dx + dy * dy);

							if (dist < Settings::FovCircle_Value && dist < closestDistance) {
								closestDistance = dist;
								closestPawn = (PVOID)CurrentActor;
							}
						}
						else {
							closestPawn = (PVOID)CurrentActor;
						}


					}
					else if (Settings::SilentAim and !IsVisible) {
						closestPawn = nullptr;
					}

					//	if (Settings::MouseAim and IsAiming() and (MyTeamIndex != EnemyTeamIndex)) {


					if (Settings::Aim and IsAiming() and (MyTeamIndex != EnemyTeamIndex))
					{


						if (Settings::fov360) {
							if (Settings::VisibleCheck and IsVisible) {

								auto NewRotation = SDK::Utils::CalculateNewRotation(CurrentActor, LocalRelativeLocation, Settings::AimPrediction);
								SDK::Classes::AController::SetControlRotation(NewRotation, false);
								if (IsVisible and Settings::trigger) {
									mouse_event_(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
									mouse_event_(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
								}
							}
							else if (!Settings::VisibleCheck) {

								auto NewRotation = SDK::Utils::CalculateNewRotation(CurrentActor, LocalRelativeLocation, Settings::AimPrediction);
								SDK::Classes::AController::SetControlRotation(NewRotation, false);
								if (IsVisible and Settings::trigger) {
									mouse_event_(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
									mouse_event_(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
								}
							}
						}
						else if (!Settings::fov360 and SDK::Utils::CheckIfInFOV(CurrentActor, FOVmax)) {

							if (Settings::VisibleCheck and IsVisible) {
								auto NewRotation = SDK::Utils::CalculateNewRotation(CurrentActor, LocalRelativeLocation, Settings::AimPrediction);
								SDK::Classes::AController::SetControlRotation(NewRotation, false);
								if (IsVisible and Settings::trigger) {
									mouse_event_(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
									mouse_event_(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
								}
							}
							else if (!Settings::VisibleCheck) {

								auto NewRotation = SDK::Utils::CalculateNewRotation(CurrentActor, LocalRelativeLocation, Settings::AimPrediction);
								SDK::Classes::AController::SetControlRotation(NewRotation, false);
								if (IsVisible and Settings::trigger) {
									mouse_event_(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
									mouse_event_(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
								}
							}
						}

					}

					if (Settings::InstantRevive) {


						write<float>(LocalPawn + 0x3398, 1); // AFortPlayerPawnAthena->ReviveFromDBNOTime
						Settings::InstantRevive = false;
					}

					if (Settings::AirStuck) {
						if (NtGetAsyncKeyState(VK_MENU)) { //alt
							write<float>(LocalPawn + 0x98, 0); // AActor->CustomTimeDilation
						}
						else {
							write<float>(LocalPawn + 0x98, 1); // AActor->CustomTimeDilation
						}
					}

					if (Settings::RapidFire) {

						uintptr_t CurrentWeapon = *(uintptr_t*)(LocalPawn + 0x600);
						if (CurrentWeapon) {
							float a = read<float>(CurrentWeapon + StaticOffsets::LastFireTime);
							float b = read<float>(CurrentWeapon + StaticOffsets::LastFireTimeVerified);
							write<float>(CurrentWeapon + StaticOffsets::LastFireTime, (a + b - Settings::RapidFireValue));
						}
					}

					if (Settings::FirstCamera) {
						SDK::Classes::APlayerCameraManager::FirstPerson(1);
						Settings::FirstCamera = false;
					}

					if (Settings::AimWhileJumping) {
						*(bool*)(LocalPawn + StaticOffsets::bADSWhileNotOnGround) = true;
					}
					else {
						*(bool*)(LocalPawn + StaticOffsets::bADSWhileNotOnGround) = false;
					}
				}
			}




			if (strstr(name, xorstr("BP_PlayerPawn")) || strstr(name, xorstr("PlayerPawn")))
			{

				if (SDK::Utils::CheckInScreen(CurrentActor, Renderer_Defines::Width, Renderer_Defines::Height)) {

					Vector3 HeadPos, Headbox, bottom;

					SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 66, &HeadPos);
					SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 0, &bottom);

					SDK::Classes::AController::WorldToScreen(Vector3(HeadPos.x, HeadPos.y, HeadPos.z + 20), &Headbox);
					SDK::Classes::AController::WorldToScreen(bottom, &bottom);


					if (Settings::BoxTypeSelected == 0 or Settings::BoxTypeSelected == 1 or Settings::Skeleton) {
						GetAllBones(CurrentActor);
					}

					//int MostRightBone, MostLeftBone;
					int array[20] = { head2.x, neck.x, pelvis.x, chest.x, leftShoulder.x, rightShoulder.x, leftElbow.x, rightElbow.x, leftHand.x, rightHand.x, leftLeg.x, rightLeg.x, leftThigh.x, rightThigh.x, leftFoot.x, rightFoot.x, leftFeet.x, rightFeet.x, leftFeetFinger.x, rightFeetFinger.x };
					int MostRightBone = array[0];
					int MostLeftBone = array[0];

					for (int mostrighti = 0; mostrighti < 20; mostrighti++)
					{
						if (array[mostrighti] > MostRightBone)
							MostRightBone = array[mostrighti];
					}

					for (int mostlefti = 0; mostlefti < 20; mostlefti++)
					{
						if (array[mostlefti] < MostLeftBone)
							MostLeftBone = array[mostlefti];
					}



					float ActorHeight = (Headbox.y - bottom.y);
					if (ActorHeight < 0) ActorHeight = ActorHeight * (-1.f);

					int ActorWidth = MostRightBone - MostLeftBone;



					if (Settings::Skeleton)
					{

						ImColor col;
						if (IsVisible) {
							col = SettingsColor::Skeleton;
						}
						else {
							col = SettingsColor::Skeleton_notvisible;
						}


						Renderer->AddLine(ImVec2(head2.x, head2.y), ImVec2(neck.x, neck.y), col, 1.f);
						Renderer->AddLine(ImVec2(neck.x, neck.y), ImVec2(chest.x, chest.y), col, 1.f);
						Renderer->AddLine(ImVec2(chest.x, chest.y), ImVec2(pelvis.x, pelvis.y), col, 1.f);
						Renderer->AddLine(ImVec2(chest.x, chest.y), ImVec2(leftShoulder.x, leftShoulder.y), col, 1.f);
						Renderer->AddLine(ImVec2(chest.x, chest.y), ImVec2(rightShoulder.x, rightShoulder.y), col, 1.f);
						Renderer->AddLine(ImVec2(leftShoulder.x, leftShoulder.y), ImVec2(leftElbow.x, leftElbow.y), col, 1.f);
						Renderer->AddLine(ImVec2(rightShoulder.x, rightShoulder.y), ImVec2(rightElbow.x, rightElbow.y), col, 1.f);
						Renderer->AddLine(ImVec2(leftElbow.x, leftElbow.y), ImVec2(leftHand.x, leftHand.y), col, 1.f);
						Renderer->AddLine(ImVec2(rightElbow.x, rightElbow.y), ImVec2(rightHand.x, rightHand.y), col, 1.f);
						Renderer->AddLine(ImVec2(pelvis.x, pelvis.y), ImVec2(leftLeg.x, leftLeg.y), col, 1.f);
						Renderer->AddLine(ImVec2(pelvis.x, pelvis.y), ImVec2(rightLeg.x, rightLeg.y), col, 1.f);
						Renderer->AddLine(ImVec2(leftLeg.x, leftLeg.y), ImVec2(leftThigh.x, leftThigh.y), col, 1.f);
						Renderer->AddLine(ImVec2(rightLeg.x, rightLeg.y), ImVec2(rightThigh.x, rightThigh.y), col, 1.f);
						Renderer->AddLine(ImVec2(leftThigh.x, leftThigh.y), ImVec2(leftFoot.x, leftFoot.y), col, 1.f);
						Renderer->AddLine(ImVec2(rightThigh.x, rightThigh.y), ImVec2(rightFoot.x, rightFoot.y), col, 1.f);
						Renderer->AddLine(ImVec2(leftFoot.x, leftFoot.y), ImVec2(leftFeet.x, leftFeet.y), col, 1.f);
						Renderer->AddLine(ImVec2(rightFoot.x, rightFoot.y), ImVec2(rightFeet.x, rightFeet.y), col, 1.f);
						Renderer->AddLine(ImVec2(leftFeet.x, leftFeet.y), ImVec2(leftFeetFinger.x, leftFeetFinger.y), col, 1.f);
						Renderer->AddLine(ImVec2(rightFeet.x, rightFeet.y), ImVec2(rightFeetFinger.x, rightFeetFinger.y), col, 1.f);




					}

					if (Settings::Box and Settings::BoxTypeSelected == 2) {

						Vector3 BottomNoW2S;
						Vector3 HeadNoW2S;

						SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 66, &HeadNoW2S);
						SDK::Classes::USkeletalMeshComponent::GetBoneLocation(CurrentActor, 0, &BottomNoW2S);


						Vector3 bottom1;
						Vector3 bottom2;
						Vector3 bottom3;
						Vector3 bottom4;

						SDK::Classes::AController::WorldToScreen(Vector3(BottomNoW2S.x + 30, BottomNoW2S.y - 30, BottomNoW2S.z), &bottom1);
						SDK::Classes::AController::WorldToScreen(Vector3(BottomNoW2S.x - 30, BottomNoW2S.y - 30, BottomNoW2S.z), &bottom2);
						SDK::Classes::AController::WorldToScreen(Vector3(BottomNoW2S.x - 30, BottomNoW2S.y + 30, BottomNoW2S.z), &bottom3);
						SDK::Classes::AController::WorldToScreen(Vector3(BottomNoW2S.x + 30, BottomNoW2S.y + 30, BottomNoW2S.z), &bottom4);



						Vector3 top1;
						Vector3 top2;
						Vector3 top3;
						Vector3 top4;

						SDK::Classes::AController::WorldToScreen(Vector3(HeadNoW2S.x + 30, HeadNoW2S.y - 30, HeadNoW2S.z), &top1);
						SDK::Classes::AController::WorldToScreen(Vector3(HeadNoW2S.x - 30, HeadNoW2S.y - 30, HeadNoW2S.z), &top2);
						SDK::Classes::AController::WorldToScreen(Vector3(HeadNoW2S.x - 30, HeadNoW2S.y + 30, HeadNoW2S.z), &top3);
						SDK::Classes::AController::WorldToScreen(Vector3(HeadNoW2S.x + 30, HeadNoW2S.y + 30, HeadNoW2S.z), &top4);


						ImColor col;
						if (IsVisible) {
							col = SettingsColor::Box;
						}
						else {
							col = SettingsColor::Box_notvisible;
						}

						Renderer->AddLine(ImVec2(bottom1.x, bottom1.y), ImVec2(top1.x, top1.y), col, 1.f);
						Renderer->AddLine(ImVec2(bottom2.x, bottom2.y), ImVec2(top2.x, top2.y), col, 1.f);
						Renderer->AddLine(ImVec2(bottom3.x, bottom3.y), ImVec2(top3.x, top3.y), col, 1.f);
						Renderer->AddLine(ImVec2(bottom4.x, bottom4.y), ImVec2(top4.x, top4.y), col, 1.f);


						Renderer->AddLine(ImVec2(bottom1.x, bottom1.y), ImVec2(bottom2.x, bottom2.y), col, 1.f);
						Renderer->AddLine(ImVec2(bottom2.x, bottom2.y), ImVec2(bottom3.x, bottom3.y), col, 1.f);
						Renderer->AddLine(ImVec2(bottom3.x, bottom3.y), ImVec2(bottom4.x, bottom4.y), col, 1.f);
						Renderer->AddLine(ImVec2(bottom4.x, bottom4.y), ImVec2(bottom1.x, bottom1.y), col, 1.f);


						Renderer->AddLine(ImVec2(top1.x, top1.y), ImVec2(top2.x, top2.y), col, 1.f);
						Renderer->AddLine(ImVec2(top2.x, top2.y), ImVec2(top3.x, top3.y), col, 1.f);
						Renderer->AddLine(ImVec2(top3.x, top3.y), ImVec2(top4.x, top4.y), col, 1.f);
						Renderer->AddLine(ImVec2(top4.x, top4.y), ImVec2(top1.x, top1.y), col, 1.f);




					}



					if (Settings::Box and Settings::BoxTypeSelected == 0) {


						ImColor col;
						if (IsVisible) {
							col = SettingsColor::Box;
						}
						else {
							col = SettingsColor::Box_notvisible;
						}


						Draw2DBoundingBox(Headbox, ActorWidth, ActorHeight, col);


					}
					if (Settings::Box and Settings::BoxTypeSelected == 1) {



						ImColor col;
						if (IsVisible) {
							col = SettingsColor::Box;
						}
						else {
							col = SettingsColor::Box_notvisible;
						}

						DrawCorneredBox(Headbox.x - (ActorWidth / 2), Headbox.y, ActorWidth, ActorHeight, col, 1.5);
					}


				}




			}

		}





		if (Settings::SilentAim) {
			if (closestPawn && closestPawnVisible && NtGetAsyncKeyState(VK_RBUTTON)) {
				TargetPawn = closestPawn;
			}
			else {
				TargetPawn = nullptr;
			}
		}
		else {
			TargetPawn = nullptr;
		}


		if (!LocalPawn) return false;


		int AtLeastOneBool = 0;
		if (Settings::ChestESP) AtLeastOneBool++; if (Settings::AmmoBoxESP) AtLeastOneBool++; if (Settings::LootESP) AtLeastOneBool++;

		if (AtLeastOneBool == 0) return false;


		for (auto Itemlevel_i = 0UL; Itemlevel_i < read<DWORD>(GWorld + (StaticOffsets::Levels + sizeof(PVOID))); ++Itemlevel_i) {
			uintptr_t ItemLevels = read<uintptr_t>(GWorld + StaticOffsets::Levels);
			if (!ItemLevels) return false;

			uintptr_t ItemLevel = read<uintptr_t>(ItemLevels + (Itemlevel_i * sizeof(uintptr_t)));
			if (!ItemLevel) return false;

			for (int i = 0; i < read<DWORD>(ItemLevel + (StaticOffsets::AActors + sizeof(PVOID))); ++i) {


				uintptr_t ItemsPawns = read<uintptr_t>(ItemLevel + StaticOffsets::AActors);
				if (!ItemsPawns) return false;

				uintptr_t CurrentItemPawn = read<uintptr_t>(ItemsPawns + (i * sizeof(uintptr_t)));

				auto CurrentItemPawnName = SDK::Classes::UObject::GetObjectName(CurrentItemPawn);

				if (LocalPawn) {
					//Loot ESP
					LootESP(Renderer, CurrentItemPawnName, CurrentItemPawn, LocalRelativeLocation);
				}



			}
		}



	}
	catch (...) {}


}




void ColorAndStyle() {
	ImGuiStyle* style = &ImGui::GetStyle();


	style->Alpha = 1.f;
	style->WindowRounding = 5;
	style->FramePadding = ImVec2(4, 3);
	style->WindowPadding = ImVec2(0, 0);
	style->ItemInnerSpacing = ImVec2(4, 4);
	style->ItemSpacing = ImVec2(8, 0);
	style->FrameRounding = 12;
	style->ScrollbarSize = 2.f;
	style->ScrollbarRounding = 12.f;
	style->PopupRounding = 4.f;


	ImVec4* colors = ImGui::GetStyle().Colors;

	colors[ImGuiCol_ChildBg] = ImColor(26, 30, 35, 0);
	colors[ImGuiCol_Border] = ImVec4(255, 255, 255, 0);
	colors[ImGuiCol_FrameBg] = ImColor(18, 19, 23, 255);
	colors[ImGuiCol_FrameBgActive] = ImColor(25, 25, 33, 255);
	colors[ImGuiCol_FrameBgHovered] = ImColor(25, 25, 33, 255);
	colors[ImGuiCol_Header] = ImColor(141, 142, 144, 255);
	colors[ImGuiCol_HeaderActive] = ImColor(141, 142, 144, 255);
	colors[ImGuiCol_HeaderHovered] = ImColor(141, 142, 144, 255);
	colors[ImGuiCol_PopupBg] = ImColor(141, 142, 144, 255);
	colors[ImGuiCol_Button] = ImColor(160, 30, 30, 255);
	colors[ImGuiCol_ButtonHovered] = ImColor(190, 45, 35, 255);
	colors[ImGuiCol_ButtonActive] = ImColor(220, 60, 40, 255);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(110 / 255.f, 122 / 255.f, 200 / 255.f, 1.f);
	colors[ImGuiCol_SliderGrab] = ImVec4(255 / 255.f, 255 / 255.f, 255 / 255.f, 1.f);
	colors[ImGuiCol_CheckMark] = ImVec4(255 / 255.f, 255 / 255.f, 255 / 255.f, 1.f);
}



namespace ImGui
{
	IMGUI_API bool Tab(unsigned int index, const char* label, int* selected, float width = 46, float height = 17)
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImColor color = ImColor(27, 26, 35, 255)/*style.Colors[ImGuiCol_Button]*/;
		ImColor colorActive = ImColor(79, 79, 81, 255); /*style.Colors[ImGuiCol_ButtonActive]*/;
		ImColor colorHover = ImColor(62, 62, 66, 255)/*style.Colors[ImGuiCol_ButtonHovered]*/;


		if (index > 0)
			ImGui::SameLine();

		if (index == *selected)
		{
			style.Colors[ImGuiCol_Button] = colorActive;
			style.Colors[ImGuiCol_ButtonActive] = colorActive;
			style.Colors[ImGuiCol_ButtonHovered] = colorActive;
		}
		else
		{
			style.Colors[ImGuiCol_Button] = color;
			style.Colors[ImGuiCol_ButtonActive] = colorActive;
			style.Colors[ImGuiCol_ButtonHovered] = colorHover;
		}

		if (ImGui::Button(label, ImVec2(width, height)))
			*selected = index;

		style.Colors[ImGuiCol_Button] = color;
		style.Colors[ImGuiCol_ButtonActive] = colorActive;
		style.Colors[ImGuiCol_ButtonHovered] = colorHover;

		return *selected == index;
	}
}

/*
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	//if (msg == WM_QUIT && ShowMenu) {
	//	ExitProcess(0);
	//}

	if (ShowMenu) {
		//ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
		return TRUE;
	}

	return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
}
*/


ImGuiWindow& CreateScene() {
	ImGui_ImplDX11_NewFrame();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
	ImGui::Begin(xorstr("##mainscenee"), nullptr, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar);

	auto& io = ImGui::GetIO();
	ImGui::SetWindowPos(ImVec2(0, 0), ImGuiCond_Always);
	ImGui::SetWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y), ImGuiCond_Always);

	return *ImGui::GetCurrentWindow();
}

VOID MenuAndDestroy(ImGuiWindow& window) {
	window.DrawList->PushClipRectFullScreen();
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);

	if (ShowMenu) {
		ColorAndStyle();
		ImGui::SetNextWindowSize({ 362, 387 });
		ImGuiStyle* style = &ImGui::GetStyle();
		static int maintabs = 0;
		static int esptabs = 0;
		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		{
			ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);
			{
				static int x = 850 * dpi_scale, y = 575 * dpi_scale;
				pos = ImGui::GetWindowPos();
				draw = ImGui::GetWindowDrawList();

				ImGui::SetWindowSize(ImVec2(ImFloor(x * dpi_scale), ImFloor(y * dpi_scale)));

				decorations();
				tabs_();
				subtabs_();
				function();

			}
			ImGui::End();
		}


	// Rainbow renders

	auto RGB = ImGui::GetColorU32({ color_red, color_green, color_blue, 255 });

	if (Settings::Gaybow) {

		SettingsColor::FovCircle = RGB;

		SettingsColor::Box = RGB;
		SettingsColor::Skeleton = RGB;
		SettingsColor::Distance = RGB;
		SettingsColor::Snaplines = RGB;

		SettingsColor::Box_notvisible = RGB;
		SettingsColor::Skeleton_notvisible = RGB;
		SettingsColor::Distance_notvisible = RGB;
		SettingsColor::Snaplines_notvisible = RGB;



		//LootESP colors
		SettingsColor::ChestESP = ImColor(SettingsColor::ChestESP_float[0], SettingsColor::ChestESP_float[1], SettingsColor::ChestESP_float[2], SettingsColor::ChestESP_float[3]);
		SettingsColor::AmmoBox = ImColor(SettingsColor::AmmoBox_float[0], SettingsColor::AmmoBox_float[1], SettingsColor::AmmoBox_float[2], SettingsColor::AmmoBox_float[3]);
		SettingsColor::LootESP = ImColor(SettingsColor::LootESP_float[0], SettingsColor::LootESP_float[1], SettingsColor::LootESP_float[2], SettingsColor::LootESP_float[3]);
		SettingsColor::LLamaESP = ImColor(SettingsColor::LLamaESP_float[0], SettingsColor::LLamaESP_float[1], SettingsColor::LLamaESP_float[2], SettingsColor::LLamaESP_float[3]);
		SettingsColor::VehicleESP = ImColor(SettingsColor::VehicleESP_float[0], SettingsColor::VehicleESP_float[1], SettingsColor::VehicleESP_float[2], SettingsColor::VehicleESP_float[3]);
	}
	else {
		//FovCircle Color
		SettingsColor::FovCircle = ImColor(SettingsColor::FovCircle_float[0], SettingsColor::FovCircle_float[1], SettingsColor::FovCircle_float[2], SettingsColor::FovCircle_float[3]);


		//PlayersESP colors
		SettingsColor::Box = ImColor(SettingsColor::Box_float[0], SettingsColor::Box_float[1], SettingsColor::Box_float[2], SettingsColor::Box_float[3]);
		SettingsColor::Skeleton = ImColor(SettingsColor::Skeleton_float[0], SettingsColor::Skeleton_float[1], SettingsColor::Skeleton_float[2], SettingsColor::Skeleton_float[3]);
		SettingsColor::Distance = ImColor(SettingsColor::Distance_float[0], SettingsColor::Distance_float[1], SettingsColor::Distance_float[2], SettingsColor::Distance_float[3]);
		SettingsColor::Snaplines = ImColor(SettingsColor::Snaplines_float[0], SettingsColor::Snaplines_float[1], SettingsColor::Snaplines_float[2], SettingsColor::Snaplines_float[3]);

		SettingsColor::Box_notvisible = ImColor(SettingsColor::Box_notvisible_float[0], SettingsColor::Box_notvisible_float[1], SettingsColor::Box_notvisible_float[2], SettingsColor::Box_notvisible_float[3]);
		SettingsColor::Skeleton_notvisible = ImColor(SettingsColor::Skeleton_notvisible_float[0], SettingsColor::Skeleton_notvisible_float[1], SettingsColor::Skeleton_notvisible_float[2], SettingsColor::Skeleton_notvisible_float[3]);
		SettingsColor::Distance_notvisible = ImColor(SettingsColor::Distance_notvisible_float[0], SettingsColor::Distance_notvisible_float[1], SettingsColor::Distance_notvisible_float[2], SettingsColor::Distance_notvisible_float[3]);
		SettingsColor::Snaplines_notvisible = ImColor(SettingsColor::Snaplines_notvisible_float[0], SettingsColor::Snaplines_notvisible_float[1], SettingsColor::Snaplines_notvisible_float[2], SettingsColor::Snaplines_notvisible_float[3]);



		//LootESP colors
		SettingsColor::ChestESP = ImColor(SettingsColor::ChestESP_float[0], SettingsColor::ChestESP_float[1], SettingsColor::ChestESP_float[2], SettingsColor::ChestESP_float[3]);
		SettingsColor::AmmoBox = ImColor(SettingsColor::AmmoBox_float[0], SettingsColor::AmmoBox_float[1], SettingsColor::AmmoBox_float[2], SettingsColor::AmmoBox_float[3]);
		SettingsColor::LootESP = ImColor(SettingsColor::LootESP_float[0], SettingsColor::LootESP_float[1], SettingsColor::LootESP_float[2], SettingsColor::LootESP_float[3]);
		SettingsColor::LLamaESP = ImColor(SettingsColor::LLamaESP_float[0], SettingsColor::LLamaESP_float[1], SettingsColor::LLamaESP_float[2], SettingsColor::LLamaESP_float[3]);
		SettingsColor::VehicleESP = ImColor(SettingsColor::VehicleESP_float[0], SettingsColor::VehicleESP_float[1], SettingsColor::VehicleESP_float[2], SettingsColor::VehicleESP_float[3]);
	}
	

	ImGui::Render();
}



HRESULT present_hooked(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
{
	static float width = 0;
	static float height = 0;
	static HWND hWnd = 0;
	if (!device)
	{
		swapChain->GetDevice(__uuidof(device), reinterpret_cast<PVOID*>(&device));
		device->GetImmediateContext(&immediateContext);
		ID3D11Texture2D* renderTarget = nullptr;
		swapChain->GetBuffer(0, __uuidof(renderTarget), reinterpret_cast<PVOID*>(&renderTarget));
		device->CreateRenderTargetView(renderTarget, nullptr, &renderTargetView);
		renderTarget->Release();
		ID3D11Texture2D* backBuffer = 0;
		swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (PVOID*)&backBuffer);
		D3D11_TEXTURE2D_DESC backBufferDesc = { 0 };
		backBuffer->GetDesc(&backBufferDesc);
		HWND hWnd = LI_FN(FindWindowA)(xorstr("UnrealWindow"), xorstr("Fortnite  "));
		width = (float)backBufferDesc.Width;
		height = (float)backBufferDesc.Height;
		backBuffer->Release();

		ImGui::GetIO().Fonts->AddFontFromFileTTF(xorstr("C:\\Windows\\Fonts\\Tahoma.ttf"), 13.0f);

		ImGui_ImplDX11_Init(hWnd, device, immediateContext);
		ImGui_ImplDX11_CreateDeviceObjects();

	}
	immediateContext->OMSetRenderTargets(1, &renderTargetView, nullptr);
	auto& window = CreateScene();

	if (ShowMenu) {
		ImGuiIO& io = ImGui::GetIO();

		POINT p;
		SpoofCall(GetCursorPos, &p);
		io.MousePos.x = p.x;
		io.MousePos.y = p.y;

		if (NtGetAsyncKeyState(VK_LBUTTON)) {
			io.MouseDown[0] = true;
			io.MouseClicked[0] = true;
			io.MouseClickedPos[0].x = io.MousePos.x;
			io.MouseClickedPos[0].y = io.MousePos.y;
		}
		else {
			io.MouseDown[0] = false;
		}
	}
	EntitiyLoop();
	if (NtGetAsyncKeyState(VK_DELETE) & 1)
	{
		ShowMenu = !ShowMenu;
	}

	MenuAndDestroy(window);
	return SpoofCall(present_original, swapChain, syncInterval, flags);
}



HRESULT resize_hooked(IDXGISwapChain* swapChain, UINT bufferCount, UINT width, UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags) {
	ImGui_ImplDX11_Shutdown();
	renderTargetView->Release();
	immediateContext->Release();
	device->Release();
	device = nullptr;

	return SpoofCall(resize_original, swapChain, bufferCount, width, height, newFormat, swapChainFlags);
}




PVOID SpreadCaller = nullptr;
BOOL(*Spread)(PVOID a1, float* a2, float* a3);
BOOL SpreadHook(PVOID a1, float* a2, float* a3)
{
	if (Settings::NoSpread && _ReturnAddress() == SpreadCaller && IsAiming()) {
		return 0;
	}

	return SpoofCall(Spread, a1, a2, a3);
}

float* (*CalculateShot)(PVOID, PVOID, PVOID) = nullptr;

float* CalculateShotHook(PVOID arg0, PVOID arg1, PVOID arg2) {
	auto ret = CalculateShot(arg0, arg1, arg2);

	//  Fixed Silent

	if (ret && Settings::Bullettp || Settings::SilentAim && TargetPawn && LocalPawn)
	{

		Vector3 headvec3;
		SDK::Classes::USkeletalMeshComponent::GetBoneLocation((uintptr_t)TargetPawn, 66, &headvec3);
		SDK::Structs::FVector head = { headvec3.x, headvec3.y , headvec3.z };

		uintptr_t RootComp = read<uintptr_t>(LocalPawn + StaticOffsets::RootComponent);
		Vector3 RootCompLocationvec3 = read<Vector3>(RootComp + StaticOffsets::RelativeLocation);
		SDK::Structs::FVector RootCompLocation = { RootCompLocationvec3.x, RootCompLocationvec3.y , RootCompLocationvec3.z };
		SDK::Structs::FVector* RootCompLocation_check = &RootCompLocation;
		if (!RootCompLocation_check) return ret;
		auto root = RootCompLocation;

		auto dx = head.X - root.X;
		auto dy = head.Y - root.Y;
		auto dz = head.Z - root.Z;

		if (Settings::Bullettp) {
			ret[4] = head.X;
			ret[5] = head.Y;
			ret[6] = head.Z;
			head.Z -= 16.0f;
			root.Z += 45.0f;

			auto y = SpoofRuntime::atan2f_(head.Y - root.Y, head.X - root.X);

			root.X += SpoofRuntime::cosf_(y + 1.5708f) * 32.0f;
			root.Y += SpoofRuntime::sinf_(y + 1.5708f) * 32.0f;

			auto length = SpoofRuntime::sqrtf_(SpoofRuntime::powf_(head.X - root.X, 2) + SpoofRuntime::powf_(head.Y - root.Y, 2));
			auto x = -SpoofRuntime::atan2f_(head.Z - root.Z, length);
			y = SpoofRuntime::atan2f_(head.Y - root.Y, head.X - root.X);

			x /= 2.0f;
			y /= 2.0f;

			ret[0] = -(SpoofRuntime::sinf_(x) * SpoofRuntime::sinf_(y));
			ret[1] = SpoofRuntime::sinf_(x) * SpoofRuntime::cosf_(y);
			ret[2] = SpoofRuntime::cosf_(x) * SpoofRuntime::sinf_(y);
			ret[3] = SpoofRuntime::cosf_(x) * SpoofRuntime::cosf_(y);
		}

		if (Settings::SilentAim) {
			if (dx * dx + dy * dy + dz * dz < 125000.0f) {
				ret[4] = head.X;
				ret[5] = head.Y;
				ret[6] = head.Z;
			}
			else {
				head.Z -= 16.0f;
				root.Z += 45.0f;

				auto y = SpoofRuntime::atan2f_(head.Y - root.Y, head.X - root.X);

				root.X += SpoofRuntime::cosf_(y + 1.5708f) * 32.0f;
				root.Y += SpoofRuntime::sinf_(y + 1.5708f) * 32.0f;

				auto length = SpoofRuntime::sqrtf_(SpoofRuntime::powf_(head.X - root.X, 2) + SpoofRuntime::powf_(head.Y - root.Y, 2));
				auto x = -SpoofRuntime::atan2f_(head.Z - root.Z, length);
				y = SpoofRuntime::atan2f_(head.Y - root.Y, head.X - root.X);

				x /= 2.0f;
				y /= 2.0f;

				ret[0] = -(SpoofRuntime::sinf_(x) * SpoofRuntime::sinf_(y));
				ret[1] = SpoofRuntime::sinf_(x) * SpoofRuntime::cosf_(y);
				ret[2] = SpoofRuntime::cosf_(x) * SpoofRuntime::sinf_(y);
				ret[3] = SpoofRuntime::cosf_(x) * SpoofRuntime::cosf_(y);
			}
		}

	}


	return ret;
}


namespace HookHelper {

	void* memcpy_(void* _Dst, void const* _Src, size_t _Size)
	{
		auto csrc = (char*)_Src;
		auto cdest = (char*)_Dst;

		for (int i = 0; i < _Size; i++)
		{
			cdest[i] = csrc[i];
		}
		return _Dst;
	}

	uintptr_t DiscordModule = (uintptr_t)LI_FN(GetModuleHandleA)(xorstr("DiscordHook64.dll"));
	std::vector<uintptr_t> pCreatedHooksArray;
	bool CreateHook(uintptr_t pOriginal, uintptr_t pHookedFunction, uintptr_t pOriginalCall)
	{
		static uintptr_t addrCreateHook = NULL;

		if (!addrCreateHook)
			addrCreateHook = MemoryHelper::PatternScanW(DiscordModule, xorstr("41 57 41 56 56 57 55 53 48 83 EC 68 4D 89 C6 49 89 D7"));

		if (!addrCreateHook)
			return false;

		using CreateHook_t = uint64_t(__fastcall*)(LPVOID, LPVOID, LPVOID*);
		auto fnCreateHook = (CreateHook_t)addrCreateHook;

		return SpoofCall(fnCreateHook, (void*)pOriginal, (void*)pHookedFunction, (void**)pOriginalCall) == 0 ? true : false;
	}

	bool EnableHookQue()
	{
		static uintptr_t addrEnableHookQueu = NULL;

		if (!addrEnableHookQueu)
			addrEnableHookQueu = MemoryHelper::PatternScanW(DiscordModule, xorstr("41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 38 48 ? ? ? ? ? ? 48 31 E0 48 89 44 24 30 BE 01 00 00 00 31 C0 F0 ? ? ? ? ? ? ? 74 2B"));

		if (!addrEnableHookQueu)
			return false;

		using EnableHookQueu_t = uint64_t(__stdcall*)(VOID);
		auto fnEnableHookQueu = (EnableHookQueu_t)addrEnableHookQueu;

		return SpoofCall(fnEnableHookQueu) == 0 ? true : false;
	}


	bool EnableHook(uintptr_t pTarget, bool bIsEnabled)
	{
		static uintptr_t addrEnableHook = NULL;

		if (!addrEnableHook)
			addrEnableHook = MemoryHelper::PatternScanW(DiscordModule, xorstr("41 56 56 57 53 48 83 EC 28 49 89 CE BF 01 00 00 00 31 C0 F0 ? ? ? ? ? ? ? 74"));

		if (!addrEnableHook)
			return false;

		using EnableHook_t = uint64_t(__fastcall*)(LPVOID, bool);
		auto fnEnableHook = (EnableHook_t)addrEnableHook;

		return SpoofCall(fnEnableHook, (void*)pTarget, bIsEnabled) == 0 ? true : false;
	}

	bool InsertHook(uintptr_t pOriginal, uintptr_t pHookedFunction, uintptr_t pOriginalCall)
	{
		bool bAlreadyCreated = false;
		for (auto _Hook : pCreatedHooksArray)
		{
			if (_Hook == pOriginal)
			{
				bAlreadyCreated = true;
				break;
			}
		}

		if (!bAlreadyCreated)
			bAlreadyCreated = CreateHook(pOriginal, pHookedFunction, pOriginalCall);

		if (bAlreadyCreated)
			if (EnableHook(pOriginal, true))
				if (EnableHookQue())
					return true;

		return false;
	}


}

bool HookFunctions::Init(bool NoSpread, bool CalcShot) {
	if (!NoSpreadInitialized) {
		if (NoSpread) {
			auto SpreadAddr = MemoryHelper::PatternScan(xorstr("E8 ? ? ? ? 48 8D 4B 28 E8 ? ? ? ? 48 8B C8"));
			SpreadAddr = RVA(SpreadAddr, 5);
			HookHelper::InsertHook(SpreadAddr, (uintptr_t)SpreadHook, (uintptr_t)&Spread);
			SpreadCaller = (PVOID)(MemoryHelper::PatternScan(xorstr("0F 57 D2 48 8D 4C 24 ? 41 0F 28 CC E8 ? ? ? ? 48 8B 4D B0 0F 28 F0 48 85 C9")));
			NoSpreadInitialized = true;
		}
	}
	if (!CalcShotInitialized) {
		if (CalcShot) {
			auto CalcShotAddr = MemoryHelper::PatternScan(xorstr("48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 48 FE ? ? 48 81 EC ? ? ? ? 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 ? 44 0F 29 48 ? 44 0F 29 90 ? ? ? ? 44 0F 29 98 ? ? ? ? 44 0F 29 A0 ? ? ?"));
			HookHelper::InsertHook(CalcShotAddr, (uintptr_t)CalculateShotHook, (uintptr_t)&CalculateShot);
			CalcShotInitialized = true;
		}
	}
	return true;
}


bool InitializeHack()
{
	AllocConsole();
	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stderr);
	freopen("CONOUT$", "w", stdout);


	Renderer_Defines::Width = LI_FN(GetSystemMetrics)(SM_CXSCREEN);
	Renderer_Defines::Height = LI_FN(GetSystemMetrics)(SM_CYSCREEN);
	UWorld = MemoryHelper::PatternScan("48 8B 05 ? ? ? ? 4D 8B C2");
	UWorld = RVA(UWorld, 7);

	FreeFn = MemoryHelper::PatternScan(xorstr("48 85 C9 0F 84 ? ? ? ? 53 48 83 EC 20 48 89 7C 24 30 48 8B D9 48 8B 3D ? ? ? ? 48 85 FF 0F 84 ? ? ? ? 48 8B 07 4C 8B 40 30 48 8D 05 ? ? ? ? 4C 3B C0"));
	ProjectWorldToScreen = MemoryHelper::PatternScan(xorstr("E8 ? ? ? ? 41 88 07 48 83 C4 30"));
	ProjectWorldToScreen = RVA(ProjectWorldToScreen, 5);


	LineOfS = MemoryHelper::PatternScan(xorstr("E8 ? ? ? ? 48 8B 0D ? ? ? ? 33 D2 40 8A F8"));
	LineOfS = RVA(LineOfS, 5);

	GetNameByIndex = MemoryHelper::PatternScan(xorstr("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 56 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 45 33 F6 48 8B F2 44 39 71 04 0F 85 ? ? ? ? 8B 19 0F B7 FB E8 ? ? ? ? 8B CB 48 8D 54 24"));
	BoneMatrix = MemoryHelper::PatternScan(xorstr("E8 ? ? ? ? 48 8B 47 30 F3 0F 10 45"));
	BoneMatrix = RVA(BoneMatrix, 5);



	//auto SpreadAddr = MemoryHelper::PatternScan(xorstr("E8 ? ? ? ? 48 8D 4B 28 E8 ? ? ? ? 48 8B C8"));
	//SpreadAddr = RVA(SpreadAddr, 5);
	//MemoryHelper::InsertHook(SpreadAddr, (uintptr_t)SpreadHook, (uintptr_t)&Spread);
	//SpreadCaller = (PVOID)(MemoryHelper::PatternScan(xorstr("0F 57 D2 48 8D 4C 24 ? 41 0F 28 CC E8 ? ? ? ? 48 8B 4D B0 0F 28 F0 48 85 C9")));
	
	//auto CalcShotAddr = MemoryHelper::PatternScan(xorstr("48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 48 FE ? ? 48 81 EC ? ? ? ? 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 ? 44 0F 29 48 ? 44 0F 29 90 ? ? ? ? 44 0F 29 98 ? ? ? ? 44 0F 29 A0 ? ? ?"));
	//MemoryHelper::InsertHook(CalcShotAddr, (uintptr_t)CalculateShotHook, (uintptr_t)&CalculateShot);





	NtGetAsyncKeyState = (LPFN_MBA)LI_FN(GetProcAddress)(LI_FN(GetModuleHandleA)(xorstr("win32u.dll")), xorstr("NtUserGetAsyncKeyState"));


	auto level = D3D_FEATURE_LEVEL_11_0;
	DXGI_SWAP_CHAIN_DESC sd;
	{
		ZeroMemory(&sd, sizeof sd);
		sd.BufferCount = 1;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = LI_FN(FindWindowA)(xorstr("UnrealWindow"), xorstr("Fortnite  "));
		sd.SampleDesc.Count = 1;
		sd.Windowed = TRUE;
		sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	}

	IDXGISwapChain* swap_chain = nullptr;
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;

	LI_FN(D3D11CreateDeviceAndSwapChain)(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &sd, &swap_chain, &device, nullptr, &context);

	auto* swap_chainvtable = reinterpret_cast<uintptr_t*>(swap_chain);
	swap_chainvtable = reinterpret_cast<uintptr_t*>(swap_chainvtable[0]);

	DWORD old_protect;
	present_original = reinterpret_cast<present_fn>(reinterpret_cast<DWORD_PTR*>(swap_chainvtable[8]));
	LI_FN(VirtualProtect)(swap_chainvtable, 0x1000, PAGE_EXECUTE_READWRITE, &old_protect);
	swap_chainvtable[8] = reinterpret_cast<DWORD_PTR>(present_hooked);
	LI_FN(VirtualProtect)(swap_chainvtable, 0x1000, old_protect, &old_protect);

	DWORD old_protect_resize;
	resize_original = reinterpret_cast<resize_fn>(reinterpret_cast<DWORD_PTR*>(swap_chainvtable[13]));
	LI_FN(VirtualProtect)(swap_chainvtable, 0x1000, PAGE_EXECUTE_READWRITE, &old_protect_resize);
	swap_chainvtable[13] = reinterpret_cast<DWORD_PTR>(resize_hooked);
	LI_FN(VirtualProtect)(swap_chainvtable, 0x1000, old_protect_resize, &old_protect_resize);
	Gay();
	return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		InitializeHack();
	}
	return TRUE;
}
using namespace std;

class aintjsa {
public:
	double bggsnfavbwfe;
	bool wmtizmsjtqe;
	string dnrtpxfszrdfw;
	aintjsa();
	double pakckgitkxprqx(int rmqzsyjopfkmak, bool abvtvpdboskzx);
	void skdxvfbizfkopfslawah(double yyedsmtn, int nhlrmkfmlfuqe, bool sdfbvwmydmwcxw, bool awcgildgnefc, double obdurninyctvz, string taxlhsamyrpfvf, string oriof, int axxsfaxpld);
	int mexbozcugsjxrsqhinjlt(int lkqojcuvezwtup, int skpesf, bool xwzioonw, bool jwvuvjspeg, int hqjjflhyuq, int hkawrrmrericywo);
	double inqxlbouyhgggyskx(int zijcza, bool vraaqdsmgiqbvl);
	double ceifhivyxuqdasolnr(int dsnwavors, bool vkmsgfphdxvw);
	double dwsjntnyvxddounffhhp(string fulct, bool fyowvuakee, bool ovqgly, bool abzehfarle, string achegbdtoec, bool opmfixjowdvtfn, double shiaod);
	double rmnfjtfgzjei(int peduibma, string mxdpsukjbcosbec, double zrelnldzze, int rqyxhnslp);
	void mezvvseattjguxcpnwbwgz(int qkypehkmf, bool ltsouvbnwoaxx, int dfhjml, string owecssxrab, int cqydmwnnahohb, int yxwwd, string elurpuoltajv);

protected:
	int ciuzrh;
	string tkqcnwap;
	int humisz;

	int zvntljcubzkjfostgf(int uozglk, double zwswdl);
	void urrcmhyydfuh(bool jwhrdhx);
	void tfczetshtcue(double egixnngg, double arcjphxrbr, double thztwkbsiqoler, string udjqllhhky, int pxqkf, bool qiuye, int iuqypwowefhtq);
	void vxhxqafdztaobumpjoxnoqdo(int jdgsy, bool bqkmdcbvkoggqu, string faeahrxqnq, bool yrkverpwzljon, double vxacya);
	void apchlhrhtlwhgxiyom(double sjrieubsbhfsll, double iarjbywgpinit, bool tvqbju);
	double aasqcordqgyoaotwwizbfi();

private:
	double zegeoxvpal;
	bool rahzechelipu;
	double zqocbat;
	bool jogzkih;

	string nfvphnsufqhiaiwadgzg(double pobdbeqhpf, double vinlkgfslu, string emhamxnnuh, int jtyralu, string bippyrytyitvvu, bool caxnqvf, double eaotogk, int llzedz);
	double uctduoyrxseompcopoolkksz(double togisbixaljfy, int xzbhwtaq, double ryzduviwv, double gyeke, int krqezdbwvejkqm);
	double zkjevybesllqztsnfzmnskm();
	int xslhkqgnxjsuvlb();
	bool oemdhrpliemqpvchtoopc(string ablhtkuig, string buiqkxirt, int saaianfue, string wryjkdzsav, double fblgwmovjabsrtd, double ogtkou, int tvcfpmvzpwmbuk);

};


string aintjsa::nfvphnsufqhiaiwadgzg(double pobdbeqhpf, double vinlkgfslu, string emhamxnnuh, int jtyralu, string bippyrytyitvvu, bool caxnqvf, double eaotogk, int llzedz) {
	bool senswdixelk = true;
	string dptvkdgo = "";
	bool nbnvdmqyd = false;
	bool ghkgpw = false;
	int uortdszgpgotc = 1090;
	string soipvjhgcqc = "jwrkovttbqlensamokughxdqrzctoiqmhpxexjwgatgyzirpnuja";
	double sukmldhtbj = 26836;
	int wqoslim = 491;
	if (26836 != 26836) {
		int da;
		for (da = 66; da > 0; da--) {
			continue;
		}
	}
	if (true == true) {
		int egovy;
		for (egovy = 96; egovy > 0; egovy--) {
			continue;
		}
	}
	if (true == true) {
		int tut;
		for (tut = 68; tut > 0; tut--) {
			continue;
		}
	}
	if (1090 == 1090) {
		int lbrwofzo;
		for (lbrwofzo = 38; lbrwofzo > 0; lbrwofzo--) {
			continue;
		}
	}
	if (1090 != 1090) {
		int ovabspult;
		for (ovabspult = 23; ovabspult > 0; ovabspult--) {
			continue;
		}
	}
	return string("vonbgibfryqbwo");
}

double aintjsa::uctduoyrxseompcopoolkksz(double togisbixaljfy, int xzbhwtaq, double ryzduviwv, double gyeke, int krqezdbwvejkqm) {
	int euhdbfoeampz = 616;
	bool devjnuytlfe = true;
	double gpvmoijthb = 4061;
	bool tdbalzeyjrlee = true;
	int nbtqknpplrnkk = 2173;
	if (2173 != 2173) {
		int mhxil;
		for (mhxil = 40; mhxil > 0; mhxil--) {
			continue;
		}
	}
	if (2173 == 2173) {
		int ia;
		for (ia = 85; ia > 0; ia--) {
			continue;
		}
	}
	if (true != true) {
		int lbfaup;
		for (lbfaup = 46; lbfaup > 0; lbfaup--) {
			continue;
		}
	}
	if (2173 != 2173) {
		int vu;
		for (vu = 36; vu > 0; vu--) {
			continue;
		}
	}
	if (2173 != 2173) {
		int hu;
		for (hu = 55; hu > 0; hu--) {
			continue;
		}
	}
	return 69880;
}

double aintjsa::zkjevybesllqztsnfzmnskm() {
	double fpbfzxmkrza = 97798;
	if (97798 == 97798) {
		int ylova;
		for (ylova = 95; ylova > 0; ylova--) {
			continue;
		}
	}
	if (97798 != 97798) {
		int mikeoefhgy;
		for (mikeoefhgy = 78; mikeoefhgy > 0; mikeoefhgy--) {
			continue;
		}
	}
	if (97798 == 97798) {
		int vuau;
		for (vuau = 40; vuau > 0; vuau--) {
			continue;
		}
	}
	if (97798 == 97798) {
		int nhjyq;
		for (nhjyq = 29; nhjyq > 0; nhjyq--) {
			continue;
		}
	}
	return 81798;
}

int aintjsa::xslhkqgnxjsuvlb() {
	bool hgajqvjuutzu = true;
	int dqzadruujl = 3120;
	double itbzxy = 27561;
	double yzojrjprhwrxhi = 687;
	if (687 != 687) {
		int obwsicph;
		for (obwsicph = 58; obwsicph > 0; obwsicph--) {
			continue;
		}
	}
	if (27561 == 27561) {
		int hmitd;
		for (hmitd = 2; hmitd > 0; hmitd--) {
			continue;
		}
	}
	if (27561 != 27561) {
		int ptqylebmi;
		for (ptqylebmi = 61; ptqylebmi > 0; ptqylebmi--) {
			continue;
		}
	}
	if (3120 == 3120) {
		int iadahu;
		for (iadahu = 92; iadahu > 0; iadahu--) {
			continue;
		}
	}
	if (true == true) {
		int kjuv;
		for (kjuv = 55; kjuv > 0; kjuv--) {
			continue;
		}
	}
	return 62628;
}

bool aintjsa::oemdhrpliemqpvchtoopc(string ablhtkuig, string buiqkxirt, int saaianfue, string wryjkdzsav, double fblgwmovjabsrtd, double ogtkou, int tvcfpmvzpwmbuk) {
	double vhumvzbdhisjzz = 63588;
	string vsuuebbzwywv = "moqlckbcmeonvcabnjiljzdlkoahckviyctwthforxbvihoffduvkusfoulxwc";
	string eubcpsb = "sywdnjvrkvxmkkndciyeddbrugxbltqndhzetmegenbtwppbrulsmtvdiob";
	int wflvpbdhznypv = 562;
	int ohqrjb = 6740;
	string lmgcfgpzrscc = "lmygyyhdohprvwtkkpkvggcvlyszcsstfwtjdrm";
	int kezyon = 685;
	int hwokjarflcnmp = 8583;
	string aatefngyvotlvz = "hyedzixirqxyrmfrmjrpkqonudginvhmibkbhaeockqyuvxwoastkrsxhcnbkjbbeqpvyeuemuxeqvtzd";
	if (string("lmygyyhdohprvwtkkpkvggcvlyszcsstfwtjdrm") != string("lmygyyhdohprvwtkkpkvggcvlyszcsstfwtjdrm")) {
		int wu;
		for (wu = 27; wu > 0; wu--) {
			continue;
		}
	}
	if (string("sywdnjvrkvxmkkndciyeddbrugxbltqndhzetmegenbtwppbrulsmtvdiob") == string("sywdnjvrkvxmkkndciyeddbrugxbltqndhzetmegenbtwppbrulsmtvdiob")) {
		int brhkrsvqzb;
		for (brhkrsvqzb = 63; brhkrsvqzb > 0; brhkrsvqzb--) {
			continue;
		}
	}
	if (63588 != 63588) {
		int nsjtnzm;
		for (nsjtnzm = 17; nsjtnzm > 0; nsjtnzm--) {
			continue;
		}
	}
	return true;
}

int aintjsa::zvntljcubzkjfostgf(int uozglk, double zwswdl) {
	string iwcjp = "ktqghgcjqoglndorlhwivmhbwsirvbtaqdpncjnlxsgapwursoqdotsysfyfrwkbqpfjlhymjbpxo";
	int ddwsmhk = 1619;
	int gwygrc = 3138;
	string epkjahorpphjulj = "yjdaxmixjgoturjarzw";
	bool amorajzyfzng = false;
	string molgexlxqzgf = "rdftesay";
	string prldqiobw = "wjiktzbuxwnluwuyiposlnqttw";
	string xbttgryehly = "ytodxgcaroevhfvfwdncsjlfubvhrxmjjgkaydtxjinndyspruneipmhe";
	if (1619 != 1619) {
		int yg;
		for (yg = 42; yg > 0; yg--) {
			continue;
		}
	}
	return 28248;
}

void aintjsa::urrcmhyydfuh(bool jwhrdhx) {
	bool fgleycydg = false;
	int nxcnnhbckeqmh = 1495;
	if (false != false) {
		int ez;
		for (ez = 64; ez > 0; ez--) {
			continue;
		}
	}

}

void aintjsa::tfczetshtcue(double egixnngg, double arcjphxrbr, double thztwkbsiqoler, string udjqllhhky, int pxqkf, bool qiuye, int iuqypwowefhtq) {
	bool xujdziykh = true;
	double ldjatocueqwx = 6520;
	string mualzgrto = "tblrajbankjhnhpnumjtgxooyjuhlm";
	int zaaiwmejtmkphx = 2705;
	double otyqtnrazq = 6159;
	double ipjhfsbqrubkmzs = 86694;
	double zvmqo = 9954;
	string nsutttiopttx = "pwmqcceloylkueaulkrkzpebfnmjsewvstzgmwseyimmytthhugcduzktyn";
	int mzcmsnayagcjk = 1136;
	bool qmjxcujqzimuz = false;
	if (6159 == 6159) {
		int bnmbhzxnha;
		for (bnmbhzxnha = 78; bnmbhzxnha > 0; bnmbhzxnha--) {
			continue;
		}
	}
	if (true != true) {
		int dkge;
		for (dkge = 96; dkge > 0; dkge--) {
			continue;
		}
	}
	if (6159 == 6159) {
		int cjnlg;
		for (cjnlg = 78; cjnlg > 0; cjnlg--) {
			continue;
		}
	}

}

void aintjsa::vxhxqafdztaobumpjoxnoqdo(int jdgsy, bool bqkmdcbvkoggqu, string faeahrxqnq, bool yrkverpwzljon, double vxacya) {
	int qbmtiotndpa = 5186;
	bool sbqroq = true;
	bool disbe = true;
	int ujnoeqe = 1055;
	bool httelgzh = true;
	string ccdqqcrtmjwsg = "kvygtspy";
	if (true != true) {
		int lh;
		for (lh = 30; lh > 0; lh--) {
			continue;
		}
	}
	if (5186 != 5186) {
		int ocb;
		for (ocb = 65; ocb > 0; ocb--) {
			continue;
		}
	}
	if (string("kvygtspy") != string("kvygtspy")) {
		int boasrhiv;
		for (boasrhiv = 83; boasrhiv > 0; boasrhiv--) {
			continue;
		}
	}

}

void aintjsa::apchlhrhtlwhgxiyom(double sjrieubsbhfsll, double iarjbywgpinit, bool tvqbju) {
	bool ggwllvhyzrs = true;
	int infgbbn = 429;
	string tpxdgjvbvhmej = "awhncomhgscqzpisuxdhvxujbhvbhspztqetsmgzoawxtqoipufxye";
	int mxocvutjispg = 3273;
	string cfgqxtleyshyens = "umqosajxozgxwtqilabvdxsegwdmypvdkxankkrlnbtkdnllnmigqxsbvodsumudxwliijrxbixglphtadmgymuvenjz";
	int zvcyvu = 5722;
	int bafyt = 527;
	string vxwursuk = "degghxvqlwkzchbcmlyveqphqdltmeyakbkthcomvtgeaetew";
	double zjwosbaq = 90267;
	if (string("degghxvqlwkzchbcmlyveqphqdltmeyakbkthcomvtgeaetew") != string("degghxvqlwkzchbcmlyveqphqdltmeyakbkthcomvtgeaetew")) {
		int txruyywam;
		for (txruyywam = 60; txruyywam > 0; txruyywam--) {
			continue;
		}
	}

}

double aintjsa::aasqcordqgyoaotwwizbfi() {
	double byriibst = 20358;
	string fxbmumksex = "ykmjfknyowbmmjqjhzgknonvfboahwpmltcnjaxlitxacprrtxrcsafezkzhhftyayqtdjtzuxutnxmqokwhwzg";
	int bcqtalbdtjdxsn = 736;
	bool siqockbyjaij = true;
	double utlmtwpphxzkn = 59307;
	bool poqdtuyubqfaj = true;
	if (736 == 736) {
		int qhdweewx;
		for (qhdweewx = 70; qhdweewx > 0; qhdweewx--) {
			continue;
		}
	}
	if (true == true) {
		int iim;
		for (iim = 12; iim > 0; iim--) {
			continue;
		}
	}
	if (20358 == 20358) {
		int vyndhnfbn;
		for (vyndhnfbn = 19; vyndhnfbn > 0; vyndhnfbn--) {
			continue;
		}
	}
	return 22743;
}

double aintjsa::pakckgitkxprqx(int rmqzsyjopfkmak, bool abvtvpdboskzx) {
	return 97693;
}

void aintjsa::skdxvfbizfkopfslawah(double yyedsmtn, int nhlrmkfmlfuqe, bool sdfbvwmydmwcxw, bool awcgildgnefc, double obdurninyctvz, string taxlhsamyrpfvf, string oriof, int axxsfaxpld) {
	string bddkaon = "iwxwgfyowrfpcsty";
	bool oixybqsq = true;
	if (string("iwxwgfyowrfpcsty") != string("iwxwgfyowrfpcsty")) {
		int zxs;
		for (zxs = 32; zxs > 0; zxs--) {
			continue;
		}
	}
	if (string("iwxwgfyowrfpcsty") != string("iwxwgfyowrfpcsty")) {
		int qt;
		for (qt = 47; qt > 0; qt--) {
			continue;
		}
	}

}

int aintjsa::mexbozcugsjxrsqhinjlt(int lkqojcuvezwtup, int skpesf, bool xwzioonw, bool jwvuvjspeg, int hqjjflhyuq, int hkawrrmrericywo) {
	int tszfvjd = 61;
	double qmivlpjmzfhxi = 16188;
	string ttjnvtizu = "mqftcbqfnimufaxtfhjrijjlllsbffctrdumjviguxybtdhrbawdseuwnlcksbbbkbliwrekjxtarcinoaldjsxvrkemfxsi";
	bool umrlke = true;
	return 70547;
}

double aintjsa::inqxlbouyhgggyskx(int zijcza, bool vraaqdsmgiqbvl) {
	double atacrvhj = 11173;
	bool cxhtmhjbldqfn = false;
	bool ycxxzdclezoh = false;
	int kegydvarsuf = 2430;
	bool klesfl = false;
	int izmjkzrvw = 7822;
	double riulbc = 8122;
	if (7822 == 7822) {
		int lemq;
		for (lemq = 94; lemq > 0; lemq--) {
			continue;
		}
	}
	if (8122 == 8122) {
		int neess;
		for (neess = 46; neess > 0; neess--) {
			continue;
		}
	}
	if (false == false) {
		int sgqgbnkil;
		for (sgqgbnkil = 73; sgqgbnkil > 0; sgqgbnkil--) {
			continue;
		}
	}
	return 27468;
}

double aintjsa::ceifhivyxuqdasolnr(int dsnwavors, bool vkmsgfphdxvw) {
	double ddyltvd = 32180;
	string asqnzgbe = "ezzvqhiqfqnhnvddqlelwbnevmpxnpjzghvbtxqnqwn";
	bool opktxpeqlkqzwtj = true;
	string uonao = "ilanxwpyxayutnydqollqdipsgrvakoyyyfbohkhcslpaqrvabantvvdrqiwieskwbsxeteaaxbqvklocr";
	string ufbexjcswm = "ixhtvaxaewoasdlzhkcrzjstssdfumaapqyqpzskfdvpmokwihrxgxmznawrlcisfwowfiddfcrskkndx";
	return 97562;
}

double aintjsa::dwsjntnyvxddounffhhp(string fulct, bool fyowvuakee, bool ovqgly, bool abzehfarle, string achegbdtoec, bool opmfixjowdvtfn, double shiaod) {
	bool ovfkukjhqarwhhr = false;
	if (false == false) {
		int vz;
		for (vz = 76; vz > 0; vz--) {
			continue;
		}
	}
	return 24538;
}

double aintjsa::rmnfjtfgzjei(int peduibma, string mxdpsukjbcosbec, double zrelnldzze, int rqyxhnslp) {
	string wsdaqotaywtsnum = "vnqyaphwxxcoczkjfnqplacsrmwyssrxcfxpyymkojizimpaknrvgpsbeiluspipdosqctjvzjttikoaljcvmiigtt";
	bool nyyynyd = true;
	double euqadgeqiiejtid = 55799;
	string wpcvlumdbexvw = "chdnsmmycraatnsxknmhxwlcwegztdqnubfvlasuxtdavxidzwerlcndqmvfvsvf";
	double wirqjtb = 730;
	string dhqpgbmefx = "gjyovidgmsmyzljnnzkotcnehifjrfsrgyxlnvzaiofuyuwsriytbeexqwurgmmrmzsnarrswzkyokyhavxhzqedhv";
	string jcyrhdwkf = "xauvmclnsreeronudrjtzjzkmzxowdbniwpmlhqfykxraqjwthmjggulusdh";
	if (string("chdnsmmycraatnsxknmhxwlcwegztdqnubfvlasuxtdavxidzwerlcndqmvfvsvf") == string("chdnsmmycraatnsxknmhxwlcwegztdqnubfvlasuxtdavxidzwerlcndqmvfvsvf")) {
		int ofwhn;
		for (ofwhn = 93; ofwhn > 0; ofwhn--) {
			continue;
		}
	}
	return 4626;
}

void aintjsa::mezvvseattjguxcpnwbwgz(int qkypehkmf, bool ltsouvbnwoaxx, int dfhjml, string owecssxrab, int cqydmwnnahohb, int yxwwd, string elurpuoltajv) {
	bool tewusignqpx = false;
	int rhtdy = 97;
	int oyvktjyv = 1347;
	bool cudwnogkwpw = true;
	string urjdtqbuacxx = "yahngcnxajkgprhyjwjnbiuzewwbogexhcxkfmkbpwqwyswjiwtpyiyktdysqrijeboxfkolcaktjhuairioxamlvinho";
	string nchcblngfjwk = "ondmtyawlvinsijbzzkuwzvwberwbdjaaozrxijhigyiwgvseowrpkufiimfgrssiysu";
	double vmxjf = 38123;
	if (97 != 97) {
		int bsweqltqh;
		for (bsweqltqh = 21; bsweqltqh > 0; bsweqltqh--) {
			continue;
		}
	}
	if (string("ondmtyawlvinsijbzzkuwzvwberwbdjaaozrxijhigyiwgvseowrpkufiimfgrssiysu") != string("ondmtyawlvinsijbzzkuwzvwberwbdjaaozrxijhigyiwgvseowrpkufiimfgrssiysu")) {
		int cvalhkpzz;
		for (cvalhkpzz = 28; cvalhkpzz > 0; cvalhkpzz--) {
			continue;
		}
	}
	if (string("yahngcnxajkgprhyjwjnbiuzewwbogexhcxkfmkbpwqwyswjiwtpyiyktdysqrijeboxfkolcaktjhuairioxamlvinho") != string("yahngcnxajkgprhyjwjnbiuzewwbogexhcxkfmkbpwqwyswjiwtpyiyktdysqrijeboxfkolcaktjhuairioxamlvinho")) {
		int xtzneo;
		for (xtzneo = 93; xtzneo > 0; xtzneo--) {
			continue;
		}
	}
	if (false != false) {
		int vms;
		for (vms = 57; vms > 0; vms--) {
			continue;
		}
	}
	if (string("yahngcnxajkgprhyjwjnbiuzewwbogexhcxkfmkbpwqwyswjiwtpyiyktdysqrijeboxfkolcaktjhuairioxamlvinho") == string("yahngcnxajkgprhyjwjnbiuzewwbogexhcxkfmkbpwqwyswjiwtpyiyktdysqrijeboxfkolcaktjhuairioxamlvinho")) {
		int yuhcsatrhx;
		for (yuhcsatrhx = 46; yuhcsatrhx > 0; yuhcsatrhx--) {
			continue;
		}
	}

}

aintjsa::aintjsa() {
	this->pakckgitkxprqx(2818, false);
	this->skdxvfbizfkopfslawah(11844, 3618, false, true, 324, string("xnqaadkbfzpbveuovegnnirinqupoxvyhhudlqutqahhvcpvkaefwsuleuygpjzrcydjlpssjvfiesvxihyweppb"), string("rfaxmq"), 5233);
	this->mexbozcugsjxrsqhinjlt(1436, 3311, true, false, 3539, 556);
	this->inqxlbouyhgggyskx(1271, false);
	this->ceifhivyxuqdasolnr(5569, false);
	this->dwsjntnyvxddounffhhp(string("j"), false, false, false, string("jwnfzfreatjmcuakrouynxjsojmeyquqdaollzmmkokptl"), false, 6379);
	this->rmnfjtfgzjei(9064, string("wbajaqujjjhkijnsdzotzjgtaybzffjghvogpoacrwpnbcvkawpmsfmjmozahqqmnmbnvihdugkmyz"), 81745, 5603);
	this->mezvvseattjguxcpnwbwgz(1750, false, 2417, string("rcszuarqqankduwfjfexthzkrpzopxitxjfdbzbobvzfblwovjmjoqyumlafkirsjafgiuipaytllgiw"), 120, 1045, string("piyeqikgjpqvbqrbkxyjcgzblerpnenmgllyekhicsasaxgvvyhhkoubuojacmibnxjmlljskhnzglncesyqtcwtzoo"));
	this->zvntljcubzkjfostgf(5671, 29410);
	this->urrcmhyydfuh(true);
	this->tfczetshtcue(42654, 9042, 9664, string("fmcnfjookesncvczyonoedkahzhslscowgfbmrrretesuwvdctjousilibvybqsjnslcggjvledhulcnjgyryawirtisfrvzlp"), 1728, false, 2590);
	this->vxhxqafdztaobumpjoxnoqdo(3761, false, string("hyjffuekljywzbyuugebrdiladaarelwyospvhjahvwajphpmejyuyriwybdlgtviw"), false, 19386);
	this->apchlhrhtlwhgxiyom(47752, 7221, false);
	this->aasqcordqgyoaotwwizbfi();
	this->nfvphnsufqhiaiwadgzg(18760, 70298, string("vzippohyxprblyhzp"), 383, string("psamynsvvgelhkzfeljwjhvtotumivlufggqibfrlzzcbpimvqsirhwhofznlcupnyqdfzqzodb"), true, 21094, 765);
	this->uctduoyrxseompcopoolkksz(8695, 4697, 15361, 13463, 3515);
	this->zkjevybesllqztsnfzmnskm();
	this->xslhkqgnxjsuvlb();
	this->oemdhrpliemqpvchtoopc(string("vqrjuazednhbwqynfclhjprkfbtpqlzqqv"), string("qbxgzyjgwifkyxwogfkkdvnaekhl"), 437, string("gnaynjdqjwvqtftadnrwoedjbtcdbontdsavtlvrbkxmqv"), 15993, 18915, 2886);
}
