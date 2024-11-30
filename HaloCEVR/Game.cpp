#define EMULATE_VR 0
#include "Game.h"
#include "Logger.h"
#include "Hooking/Hooks.h"
#include "Helpers/DX9.h"
#include "Helpers/RenderTarget.h"
#include "Helpers/Renderer.h"
#include "Helpers/Camera.h"
#include "Helpers/Menus.h"
#include "Helpers/Objects.h"
#include "Helpers/Maths.h"

#if EMULATE_VR
#include "VR/VREmulator.h"
#else
#include "VR/OpenVR.h"
#endif


void Game::Init()
{
	Logger::log << "[Game] HaloCEVR initialising..." << std::endl;

	SetupConfigs();

	CreateConsole();

	PatchGame();

#if EMULATE_VR
	vr = new VREmulator();
#else
	vr = new OpenVR();
#endif

	vr->Init();

	inputHandler.RegisterInputs();

	backBufferWidth = vr->GetViewWidth();
	backBufferHeight = vr->GetViewHeight();

	bHasShutdown = false;

	Logger::log << "[Game] HaloCEVR initialised" << std::endl;
}

void Game::Shutdown()
{
	if (bHasShutdown)
	{
		return;
	}
	bHasShutdown = true;

	Logger::log << "[Game] HaloCEVR shutting down..." << std::endl;

	vr->Shutdown();

	MH_STATUS hookStatus = MH_DisableHook(MH_ALL_HOOKS);

	if (hookStatus != MH_OK)
	{
		Logger::log << "[Game] Could not remove hooks: " << MH_StatusToString(hookStatus) << std::endl;
	}

	hookStatus = MH_Uninitialize();

	if (hookStatus != MH_OK)
	{
		Logger::log << "[Game] Could not uninitialise MinHook: " << MH_StatusToString(hookStatus) << std::endl;
	}

	if (c_ShowConsole && c_ShowConsole->Value())
	{
		if (consoleOut)
		{
			fclose(consoleOut);
		}
		FreeConsole();
	}
}

void Game::OnInitDirectX()
{
	Logger::log << "[Game] Game has finished DirectX initialisation" << std::endl;

	if (!Helpers::GetDirect3DDevice9())
	{
		Logger::err << "Couldn't get game's direct3d device" << std::endl;
		return;
	}

	SetForegroundWindow(GetActiveWindow());

	vr->OnGameFinishInit();

	uiSurface = vr->GetUISurface();
	crosshairSurface = vr->GetCrosshairSurface();

	scopeSurfaces[0] = vr->GetScopeSurface();
	scopeTextures[0] = vr->GetScopeTexture();

	D3DSURFACE_DESC desc;
	scopeSurfaces[0]->GetDesc(&desc);

	CreateTextureAndSurface(desc.Width, desc.Height, desc.Usage, desc.Format, &scopeSurfaces[1], &scopeTextures[1]);
	CreateTextureAndSurface(desc.Width / 2, desc.Height / 2, desc.Usage, desc.Format, &scopeSurfaces[2], &scopeTextures[2]);
}

void Game::PreDrawFrame(struct Renderer* renderer, float deltaTime)
{
	lastDeltaTime = deltaTime;

	renderState = ERenderState::UNKNOWN;

	bool bIsLoading = Helpers::IsCampaignLoading();

	if (bWasLoading && !bIsLoading)
	{
		bNeedsRecentre = true;
	}
	bWasLoading = bIsLoading;

	//CalcFPS(deltaTime);

	vr->SetMouseVisibility(Helpers::IsMouseVisible());
	vr->UpdatePoses();

	UpdateCrosshairAndScope();

	StoreRenderTargets();

	sRect* window = Helpers::GetWindowRect();
	window->top = 0;
	window->left = 0;
	window->right = vr->GetViewWidth();
	window->bottom = vr->GetViewHeight();

	// Clear UI surfaces
	IDirect3DSurface9* currentSurface = nullptr;
	Helpers::GetDirect3DDevice9()->GetRenderTarget(0, &currentSurface);
	Helpers::GetDirect3DDevice9()->SetRenderTarget(0, uiSurface);
	Helpers::GetDirect3DDevice9()->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
	Helpers::GetDirect3DDevice9()->SetRenderTarget(0, crosshairSurface);
	Helpers::GetDirect3DDevice9()->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
	Helpers::GetDirect3DDevice9()->SetRenderTarget(0, scopeSurfaces[0]);
	Helpers::GetDirect3DDevice9()->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
	Helpers::GetDirect3DDevice9()->SetRenderTarget(0, currentSurface);
	currentSurface->Release();

	frustum1 = renderer->frustum;
	frustum2 = renderer->frustum2;

	if (bNeedsRecentre)
	{
		bNeedsRecentre = false;
		vr->Recentre();
	}

	UnitDynamicObject* Player = static_cast<UnitDynamicObject*>(Helpers::GetLocalPlayer());
	if (Player)
	{
		bool bNewShowViewModel = Player->parent.id != 0xffff;

		if (bNewShowViewModel != bShowViewModel)
		{
			// Self modifying code is the best code
			Hooks::P_KeepViewModelVisible(bNewShowViewModel);

			bShowViewModel = bNewShowViewModel;
		}
		bInVehicle = bNewShowViewModel;
		bHasWeapon = Player->weapon.id != 0xffff;
	}

	if (c_ShowRoomCentre->Value())
	{
		Vector3 position = Helpers::GetCamera().position;
		position.z -= 0.62f;
		Vector3 upVector(0.0f, 0.0f, 1.0f);
		Vector3 forwardVector(1.0f, 0.0f, 0.0f);

		inGameRenderer.DrawPolygon(position, upVector, forwardVector, 8, MetresToWorld(0.25f), D3DCOLOR_ARGB(50, 85, 250, 239), false);
	}

#if 0
	// Debug draw controller position (why, oh why, does steamvr make getting a consistent controller position/orientation so hard?)
	Matrix4 controller = vr->GetControllerTransform(ControllerRole::Right);

	Matrix3 handRotation3;

	for (int i = 0; i < 3; i++)
	{
		handRotation3.setColumn(i, &controller.get()[i * 4]);
	}

	Vector3 worldPos = Helpers::GetCamera().position;

	inGameRenderer.DrawCoordinate(controller * Vector3(0.0f, 0.0f, 0.0f) * MetresToWorld(1.0f) + worldPos, handRotation3, 0.05f, false);

	for (int i = 0; i < 31; i++)
	{
		Matrix4 bone = vr->GetControllerBoneTransform(ControllerRole::Right, i);

		for (int i = 0; i < 3; i++)
		{
			handRotation3.setColumn(i, &bone.get()[i * 4]);
		}

		inGameRenderer.DrawCoordinate(bone * Vector3(0.0f, 0.0f, 0.0f) * MetresToWorld(1.0f) + worldPos, handRotation3, i == 1 ? 0.05f : 0.005f, false);
	}

	controller = vr->GetControllerTransform(ControllerRole::Right);

	for (int i = 0; i < 3; i++)
	{
		handRotation3.setColumn(i, &controller.get()[i * 4]);
	}

	inGameRenderer.DrawCoordinate(controller * Vector3(0.0f, 0.0f, 0.0f) * MetresToWorld(1.0f) + worldPos, handRotation3, 0.05f, false);
#endif

	vr->PreDrawFrame(renderer, deltaTime);
}

void Game::PreDrawEye(Renderer* renderer, float deltaTime, int eye)
{
	renderState = eye == 0 ? ERenderState::LEFT_EYE : ERenderState::RIGHT_EYE;

	renderer->frustum = frustum1;
	renderer->frustum2 = frustum2;

	vr->UpdateCameraFrustum(&renderer->frustum, eye);
	vr->UpdateCameraFrustum(&renderer->frustum2, eye);

	RenderTarget* primaryRenderTarget = Helpers::GetRenderTargets();

	primaryRenderTarget[0].renderSurface = vr->GetRenderSurface(eye);
	primaryRenderTarget[0].renderTexture = vr->GetRenderTexture(eye);
	primaryRenderTarget[0].width = vr->GetViewWidth();
	primaryRenderTarget[0].height = vr->GetViewHeight();

	IDirect3DSurface9* Current;

	inGameRenderer.ExtractMatrices(renderer);
}


void Game::PostDrawEye(struct Renderer* renderer, float deltaTime, int eye)
{
	if (Helpers::IsLoading() || Helpers::IsCampaignLoading())
	{
		Helpers::GetDirect3DDevice9()->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
	}

	inGameRenderer.Render(Helpers::GetDirect3DDevice9());
}

bool Game::PreDrawScope(Renderer* renderer, float deltaTime)
{
	UnitDynamicObject* player = static_cast<UnitDynamicObject*>(Helpers::GetLocalPlayer());

	if (!player || player->zoom == -1)
	{
		return false;
	}

	renderState = ERenderState::SCOPE;

	renderer->frustum = frustum1;
	renderer->frustum2 = frustum2;

	Vector3 aimPos, aimDir, upDir;

	weaponHandler.GetWorldWeaponAim(aimPos, aimDir);

	upDir = Vector3(0.0f, 0.0f, 1.0f);
	upDir = aimDir.cross(upDir);
	upDir = upDir.cross(aimDir);

	renderer->frustum.position = aimPos;
	renderer->frustum2.position = aimPos;
	renderer->frustum.facingDirection = aimDir;
	renderer->frustum2.facingDirection = aimDir;
	renderer->frustum.upDirection = upDir;
	renderer->frustum2.upDirection = upDir;

	RenderTarget* primaryRenderTarget = Helpers::GetRenderTargets();

	primaryRenderTarget[0].renderSurface = scopeSurfaces[0];
	primaryRenderTarget[0].renderTexture = scopeTextures[0];
	primaryRenderTarget[0].width = vr->GetScopeWidth();
	primaryRenderTarget[0].height = vr->GetScopeHeight();
	primaryRenderTarget[1].renderSurface = scopeSurfaces[1];
	primaryRenderTarget[1].renderTexture = scopeTextures[1];
	primaryRenderTarget[1].width = vr->GetScopeWidth();
	primaryRenderTarget[1].height = vr->GetScopeHeight();
	primaryRenderTarget[2].renderSurface = scopeSurfaces[2];
	primaryRenderTarget[2].renderTexture = scopeTextures[2];
	primaryRenderTarget[2].width = vr->GetScopeWidth() / 2;
	primaryRenderTarget[2].height = vr->GetScopeHeight() / 2;


	sRect* windowMain = Helpers::GetWindowRect();
	windowMain->top = 0;
	windowMain->left = 0;
	windowMain->right = vr->GetScopeWidth();
	windowMain->bottom = vr->GetScopeHeight();

	{
		sRect& window = renderer->frustum.WindowViewport;
		window.top = 0;
		window.left = 0;
		window.right = vr->GetScopeWidth();
		window.bottom = vr->GetScopeHeight();

		sRect& window2 = renderer->frustum2.WindowViewport;
		window2.top = 0;
		window2.left = 0;
		window2.right = vr->GetScopeWidth();
		window2.bottom = vr->GetScopeHeight();
	}

	return true;
}

void Game::PostDrawScope(Renderer* renderer, float deltaTime)
{
	RestoreRenderTargets();

	Vector2 innerSize = Vector2(0.0f, 0.0f);
	Vector2 size = Vector2(static_cast<float>(vr->GetScopeWidth()), static_cast<float>(vr->GetScopeHeight()));
	Vector2 centre = size / 2;
	int sides = 32;
	float radius = size.y * 0.25f;
	D3DCOLOR color = D3DCOLOR_ARGB(255, 0, 0, 0);

	scopeRenderer.ExtractMatrices(renderer);

	// Sniper scope is a rounded square, so we need to separate the quadrants and change the radius
	if (weaponHandler.IsSniperScope())
	{
		radius = size.y * 0.03125f;
		const float scopeWidth = 0.605f * size.x - radius * 2.0f;
		const float scopeHeight = 0.505f * size.y - radius * 2.0f;
		innerSize = Vector2(scopeWidth, scopeHeight);
	}

	scopeRenderer.DrawInvertedShape2D(centre, innerSize, size, sides, radius, color);

	scopeRenderer.Render(Helpers::GetDirect3DDevice9());
	scopeRenderer.PostRender();

}

void Game::PreDrawMirror(struct Renderer* renderer, float deltaTime)
{
	renderState = ERenderState::GAME;

	renderer->frustum = frustum1;
	renderer->frustum2 = frustum2;

	RestoreRenderTargets();

	sRect* windowMain = Helpers::GetWindowRect();
	windowMain->top = 0;
	windowMain->left = 0;
	windowMain->right = Helpers::GetRenderTargets()[0].width;
	windowMain->bottom = Helpers::GetRenderTargets()[0].height;

	inGameRenderer.ExtractMatrices(renderer);
}

void Game::PostDrawMirror(struct Renderer* renderer, float deltaTime)
{
	// Do something here to copy the image into the backbuffer correctly

	inGameRenderer.ClearRenderTargets();
	inGameRenderer.Render(Helpers::GetDirect3DDevice9());
}

void Game::PostDrawFrame(struct Renderer* renderer, float deltaTime)
{
	RestoreRenderTargets();
	vr->PostDrawFrame(renderer, deltaTime);
	inGameRenderer.PostRender();

	sRect* windowMain = Helpers::GetWindowRect();
	windowMain->top = 0;
	windowMain->left = 0;
	windowMain->right = Helpers::GetRenderTargets()[0].width;
	windowMain->bottom = Helpers::GetRenderTargets()[0].height;

	if (c_DrawMirror->Value() && mirrorSource != ERenderState::GAME)
	{
		int sWidth = vr->GetViewWidth();
		int sHeight = vr->GetViewHeight();

		float sourceAspect = static_cast<float>(sWidth) / static_cast<float>(sHeight);

		int dWidth = Helpers::GetRenderTargets()[0].width;
		int dHeight = Helpers::GetRenderTargets()[0].height;

		int trueWidth = 640;
		int trueHeight = 480;

		float destAspect = static_cast<float>(trueWidth) / static_cast<float>(trueHeight);

		RECT destRect{};

		if (sourceAspect > destAspect)
		{
			destRect.left = 0;
			destRect.right = dWidth;

			float scale = destAspect / sourceAspect;

			int scaledSize = static_cast<int>(0.5f * (1.0f - scale) * dHeight);

			destRect.top = scaledSize;
			destRect.bottom = dHeight - scaledSize;
		}
		else
		{
			destRect.top = 0;
			destRect.bottom = dHeight;

			float scale = sourceAspect / destAspect;

			int scaledSize = static_cast<int>(0.5f * (1.0f - scale) * dWidth);

			destRect.left = scaledSize;
			destRect.right = dWidth - scaledSize;
		}


		int eye = mirrorSource == ERenderState::LEFT_EYE ? 0 : 1;

		IDirect3DSurface9* currentSurface = nullptr;
		Helpers::GetDirect3DDevice9()->GetRenderTarget(0, &currentSurface);
		Helpers::GetDirect3DDevice9()->SetRenderTarget(0, Helpers::GetRenderTargets()[0].renderSurface);
		Helpers::GetDirect3DDevice9()->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
		Helpers::GetDirect3DDevice9()->SetRenderTarget(0, currentSurface);
		currentSurface->Release();
		Helpers::GetDirect3DDevice9()->StretchRect(vr->GetRenderSurface(eye), nullptr, Helpers::GetRenderTargets()[0].renderSurface, &destRect, D3DTEXF_LINEAR);
	}
}

bool Game::PreDrawHUD()
{
	// Only render UI once per frame
	if (GetRenderState() != ERenderState::LEFT_EYE)
	{
		// Remove zoom effect from game view
		if (GetRenderState() == ERenderState::GAME)
		{
			short* zoom = &Helpers::GetInputData().zoomLevel;
			realZoom = *zoom;
			*zoom = -1;
		}

		// ...but try to avoid breaking the game view (for now at least)
		return GetRenderState() == ERenderState::GAME || GetRenderState() == ERenderState::SCOPE;
	}

	short* zoom = &Helpers::GetInputData().zoomLevel;
	realZoom = *zoom;
	*zoom = -1;

	Helpers::GetDirect3DDevice9()->GetRenderTarget(0, &uiRealSurface);
	Helpers::GetDirect3DDevice9()->SetRenderTarget(0, uiSurface);
	uiRealSurface = Helpers::GetRenderTargets()[1].renderSurface;
	Helpers::GetRenderTargets()[1].renderSurface = uiSurface;
	realUIWidth = Helpers::GetRenderTargets()[1].width;
	realUIHeight = Helpers::GetRenderTargets()[1].height;
	Helpers::GetRenderTargets()[1].width = c_UIOverlayWidth->Value();
	Helpers::GetRenderTargets()[1].height = c_UIOverlayHeight->Value();

	sRect* windowMain = Helpers::GetCurrentRect();
	realRect = *windowMain;

	windowMain->top = 0;
	windowMain->left = 0;
	windowMain->right = c_UIOverlayWidth->Value();
	windowMain->bottom = c_UIOverlayHeight->Value();

	return true;
}

void Game::PostDrawHUD()
{
	// Only render UI once per frame
	if (GetRenderState() != ERenderState::LEFT_EYE)
	{
		// Remove zoom effect from game view
		if (GetRenderState() == ERenderState::GAME)
		{
			short* zoom = &Helpers::GetInputData().zoomLevel;
			*zoom = realZoom;
		}

		return;
	}

	short* zoom = &Helpers::GetInputData().zoomLevel;
	*zoom = realZoom;

	sRect* windowMain = Helpers::GetCurrentRect();
	*windowMain = realRect;

	Helpers::GetRenderTargets()[1].width = realUIWidth;
	Helpers::GetRenderTargets()[1].height = realUIHeight;

	Helpers::GetRenderTargets()[1].renderSurface = uiRealSurface;
	Helpers::GetDirect3DDevice9()->SetRenderTarget(0, uiRealSurface);

}

bool Game::PreDrawMenu()
{
	// Only render UI once per frame
	if (GetRenderState() != ERenderState::LEFT_EYE)
	{
		// ...but try to avoid breaking the game view (for now at least)
		return GetRenderState() == ERenderState::GAME;
	}

	Helpers::GetDirect3DDevice9()->GetRenderTarget(0, &uiRealSurface);
	Helpers::GetDirect3DDevice9()->SetRenderTarget(0, uiSurface);
	uiRealSurface = Helpers::GetRenderTargets()[1].renderSurface;
	Helpers::GetRenderTargets()[1].renderSurface = uiSurface;

	return true;
}

void Game::PostDrawMenu()
{
	// Only render UI once per frame
	if (GetRenderState() != ERenderState::LEFT_EYE)
	{
		return;
	}

	Helpers::GetRenderTargets()[1].renderSurface = uiRealSurface;
	Helpers::GetDirect3DDevice9()->SetRenderTarget(0, uiRealSurface);
}

D3DVIEWPORT9 currentViewport;
bool Game::PreDrawLoading(int param1, struct Renderer* renderer)
{
	// Only render UI once per frame
	if (GetRenderState() != ERenderState::LEFT_EYE)
	{
		// ...but try to avoid breaking the game view (for now at least)
		return GetRenderState() == ERenderState::GAME;
	}

	uiRealSurface = Helpers::GetRenderTargets()[1].renderSurface;
	Helpers::GetRenderTargets()[1].renderSurface = uiSurface;

	return true;
}

void Game::PostDrawLoading(int param1, struct Renderer* renderer)
{
	// Only render UI once per frame
	if (GetRenderState() != ERenderState::LEFT_EYE)
	{
		return;
	}

	Helpers::GetRenderTargets()[1].renderSurface = uiRealSurface;
}

bool Game::PreDrawCrosshair(short* anchorLocation)
{
	// Draw things normally for the scope
	if (GetRenderState() == ERenderState::SCOPE)
	{
		return true;
	}

	crosshairRealSurface = Helpers::GetRenderTargets()[1].renderSurface;
	if (anchorLocation && *anchorLocation == 4) // Centre = 4
	{
		if (realZoom != -1)
		{
			return false;
		}
		Helpers::GetRenderTargets()[1].renderSurface = crosshairSurface;
		Helpers::GetDirect3DDevice9()->SetRenderTarget(0, crosshairSurface);
	}

	return true;
}

void Game::PostDrawCrosshair()
{
	// Draw things normally for the scope
	if (GetRenderState() == ERenderState::SCOPE)
	{
		return;
	}

	Helpers::GetRenderTargets()[1].renderSurface = crosshairRealSurface;
	Helpers::GetDirect3DDevice9()->SetRenderTarget(0, crosshairRealSurface);
}

void Game::PreDrawImage(void* param1, void* param2)
{
	Helpers::GetDirect3DDevice9()->GetRenderState(D3DRS_ALPHAFUNC, &realAlphaFunc);
	Helpers::GetDirect3DDevice9()->GetRenderState(D3DRS_SRCBLENDALPHA, &realAlphaSrc);
	Helpers::GetDirect3DDevice9()->GetRenderState(D3DRS_DESTBLENDALPHA, &realAlphaDest);

	//Logger::log << realAlphaFunc << ", " << realAlphaSrc << ", " << realAlphaDest << std::endl;

	Helpers::GetDirect3DDevice9()->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	Helpers::GetDirect3DDevice9()->SetRenderState(D3DRS_ALPHAFUNC, D3DBLENDOP_ADD);
	Helpers::GetDirect3DDevice9()->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_SRCALPHA);
	Helpers::GetDirect3DDevice9()->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
}

void Game::PostDrawImage(void* param1, void* param2)
{
	DWORD tmp1, tmp2, tmp3;

	Helpers::GetDirect3DDevice9()->GetRenderState(D3DRS_ALPHAFUNC, &tmp1);
	Helpers::GetDirect3DDevice9()->GetRenderState(D3DRS_SRCBLENDALPHA, &tmp2);
	Helpers::GetDirect3DDevice9()->GetRenderState(D3DRS_DESTBLENDALPHA, &tmp3);

	//Logger::log << tmp1 << ", " << tmp2 << ", " << tmp3 << std::endl;

	Helpers::GetDirect3DDevice9()->SetRenderState(D3DRS_ALPHAFUNC, realAlphaFunc);
	Helpers::GetDirect3DDevice9()->SetRenderState(D3DRS_SRCBLENDALPHA, realAlphaSrc);
	Helpers::GetDirect3DDevice9()->SetRenderState(D3DRS_DESTBLENDALPHA, realAlphaDest);
}

void Game::UpdateViewModel(HaloID& id, Vector3* pos, Vector3* facing, Vector3* up, TransformQuat* BoneTransforms, Transform* OutBoneTransforms)
{
	weaponHandler.UpdateViewModel(id, pos, facing, up, BoneTransforms, OutBoneTransforms);
}

void Game::PreFireWeapon(HaloID& weaponID, short param2)
{
	weaponHandler.PreFireWeapon(weaponID, param2);
}

void Game::PostFireWeapon(HaloID& weaponID, short param2)
{
	weaponHandler.PostFireWeapon(weaponID, param2);
}

void Game::PreThrowGrenade(HaloID& playerID)
{
	weaponHandler.PreThrowGrenade(playerID);
}

void Game::PostThrowGrenade(HaloID& playerID)
{
	weaponHandler.PostThrowGrenade(playerID);
}

void Game::UpdateInputs()
{
	inputHandler.UpdateInputs();
}


void Game::UpdateCamera(float& yaw, float& pitch)
{
	// Don't bother simulating inputs if we aren't actually in vr
#if EMULATE_VR
	return;
#endif

	if (bInVehicle && !bHasWeapon)
	{
		inputHandler.UpdateCameraForVehicles(yaw, pitch);
	}
	else
	{
		inputHandler.UpdateCamera(yaw, pitch);
	}
}

void Game::SetMousePosition(int& x, int& y)
{
	// Don't bother simulating inputs if we aren't actually in vr
#if EMULATE_VR
	return;
#endif
	inputHandler.SetMousePosition(x, y);
}

void Game::UpdateMouseInfo(MouseInfo* mouseInfo)
{
	// Don't bother simulating inputs if we aren't actually in vr
#if EMULATE_VR
	return;
#endif
	inputHandler.UpdateMouseInfo(mouseInfo);
}

void Game::SetViewportScale(Viewport* viewport)
{
	viewport->left = -1.0f;
	viewport->right = 1.0f;
	viewport->bottom = 1.0f;
	viewport->top = -1.0f;
}

float Game::MetresToWorld(float m) const
{
	return m / 3.048f;
}

float Game::WorldToMetres(float w) const
{
	return w * 3.048f;
}

void Game::CreateConsole()
{
	if (!c_ShowConsole || !c_ShowConsole->Value())
	{
		return;
	}

	AllocConsole();
	freopen_s(&consoleOut, "CONOUT$", "w", stdout);
	std::cout.clear();
}

void Game::PatchGame()
{
	MH_STATUS hookStatus;

	if ((hookStatus = MH_Initialize()) != MH_OK)
	{
		Logger::err << "Could not initialise MinHook: " << MH_StatusToString(hookStatus) << std::endl;
	}
	else
	{
		Logger::log << "[Game] Creating hooks" << std::endl;
		Hooks::InitHooks();
		Logger::log << "[Game] Enabling hooks" << std::endl;
		Hooks::EnableAllHooks();
	}
}

void Game::SetupConfigs()
{
	Config config;

	// Window settings
	c_ShowConsole = config.RegisterBool("ShowConsole", "Create a console window at launch for debugging purposes", false);
	c_DrawMirror = config.RegisterBool("DrawMirror", "Update the desktop window display to show the current game view, rather than leaving it on the splash screen", true);
	c_MirrorEye = config.RegisterInt("MirrorEye", "Index of the eye to use for the mirror view  (0 = left, 1 = right)", 0);
	// UI settings
	c_UIOverlayDistance = config.RegisterFloat("UIOverlayDistance", "Distance in metres in front of the HMD to display the UI", 15.0f);
	c_UIOverlayScale = config.RegisterFloat("UIOverlayScale", "Width of the UI overlay in metres", 10.0f);
	c_UIOverlayCurvature = config.RegisterFloat("UIOverlayCurvature", "Curvature of the UI Overlay, on a scale of 0 to 1", 0.1f);
	c_UIOverlayWidth = config.RegisterInt("UIOverlayWidth", "Width of the UI overlay in pixels", 800);
	c_UIOverlayHeight = config.RegisterInt("UIOverlayHeight", "Height of the UI overlay in pixels", 600);
	// Control settings
	c_LeftHanded = config.RegisterBool("LeftHanded", "Make the left hand the dominant hand. Does not affect bindings, change these in the SteamVR overlay", false);
	c_SnapTurn = config.RegisterBool("SnapTurn", "The look input will instantly rotate the view by a fixed amount, rather than smoothly rotating", true);
	c_SnapTurnAmount = config.RegisterFloat("SnapTurnAmount", "Rotation in degrees a single snap turn will rotate the view by", 45.0f);
	c_SmoothTurnAmount = config.RegisterFloat("SmoothTurnAmount", "Rotation in degrees per second the view will turn at when not using snap turning", 90.0f);
	c_HorizontalVehicleTurnAmount = config.RegisterFloat("HorizontalVehicleTurnAmount", "Rotation in degrees per second the view will turn horizontally when in vehicles", 90.0f);
	c_VerticalVehicleTurnAmount = config.RegisterFloat("VerticalVehicleTurnAmount", "Rotation in degrees per second the view will turn vertically when in vehicles", 45.0f);
	c_ToggleGrip = config.RegisterBool("ToggleGrip", "When true releasing two handed weapons requires pressing the grip action again", false);
	c_LeftHandFlashlightDistance = config.RegisterFloat("LeftHandFlashlight", "Bringing the left hand within this distance of the head will toggle the flashlight (<0 to disable)", 0.2f);
	c_RightHandFlashlightDistance = config.RegisterFloat("RightHandFlashlight", "Bringing the right hand within this distance of the head will toggle the flashlight (<0 to disable)", -1.0f);
	c_MeleeSwingSpeed = config.RegisterFloat("MeleeSwingSpeed", "Minimum vertical velocity of either hand required to initiate a melee attack in m/s", 2.5f);
	c_CrouchHeight = config.RegisterFloat("CrouchHeight", "Minimum height to duck by in metres to automatically trigger the crouch input in game (<0 to disable)", 0.15f);
	// Hand settings
	c_ControllerOffset = config.RegisterVector3("ControllerOffset", "Offset from the controller's position used when calculating the in game hand position", Vector3(0.0f, 0.0f, 0.0f));
	c_ControllerRotation = config.RegisterVector3("ControllerRotation", "Rotation added to the controller when calculating the in game hand rotation", Vector3(0.0f, 0.0f, 0.0f));
	c_ScopeRenderScale = config.RegisterFloat("ScopeRenderScale", "Size of the scope render target, expressed as a proportion of the headset's render scale (e.g. 0.5 = half resolution)", 1.0f);
	c_ScopeScale = config.RegisterFloat("ScopeScale", "Width of the scope view in metres", 0.05f);
	c_ScopeOffsetPistol = config.RegisterVector3("ScopeOffsetPistol", "Offset of the scope view relative to the pistol's location", Vector3(-0.1f, 0.0f, 0.15f));
	c_ScopeOffsetSniper = config.RegisterVector3("ScopeOffsetSniper", "Offset of the scope view relative to the pistol's location", Vector3(-0.15f, 0.0f, 0.15f));
	c_ScopeOffsetRocket = config.RegisterVector3("ScopeOffsetRocket", "Offset of the scope view relative to the pistol's location", Vector3(0.1f, 0.2f, 0.1f));
	// Misc settings
	c_ShowRoomCentre = config.RegisterBool("ShowRoomCentre", "Draw an indicator at your feet to show where the player character is actually positioned", true);
	c_d3d9Path = config.RegisterString("CustomD3D9Path", "If set first try to load d3d9.dll from the specified path instead of from system32", "");

	config.LoadFromFile("VR/config.txt");
	config.SaveToFile("VR/config.txt");

	weaponHandler.localOffset = Vector3(c_ControllerOffset->Value().x, c_ControllerOffset->Value().y, c_ControllerOffset->Value().z);
	weaponHandler.localRotation = Vector3(c_ControllerRotation->Value().x, c_ControllerRotation->Value().y, c_ControllerRotation->Value().z);

	if (c_MirrorEye->Value() == 0)
	{
		mirrorSource = ERenderState::LEFT_EYE;
	}
	else if (c_MirrorEye->Value() == 1)
	{
		mirrorSource = ERenderState::RIGHT_EYE;
	}
	else if (c_MirrorEye->Value() == 2)
	{
		Logger::log << "[Config] MirrorEye set to 'game'. This is intended for debugging, may not look correct, and will likely impact performance" << std::endl;
		mirrorSource = ERenderState::GAME;
	}
	else
	{
		Logger::log << "[Config] Invalid value for MirrorEye, defaulting to left eye" << std::endl;
		mirrorSource = ERenderState::LEFT_EYE;
	}

	//Logger::log << "[Config] Loaded configs" << std::endl;
}

void Game::CalcFPS(float deltaTime)
{
	fpsTracker.framesSinceFPSUpdate++;
	fpsTracker.timeSinceFPSUpdate += deltaTime;

	if (fpsTracker.timeSinceFPSUpdate > 1.0f)
	{
		fpsTracker.timeSinceFPSUpdate = 0.0f;
		fpsTracker.fps = fpsTracker.framesSinceFPSUpdate;
		fpsTracker.framesSinceFPSUpdate = 0;
		Logger::log << fpsTracker.fps << std::endl;
	}
}

void Game::UpdateCrosshairAndScope()
{
	auto fixupRotation = [](Matrix4& m, Vector3& pos) {
		m.translate(-pos);
		m.rotate(90.0f, m.getUpAxis());
		m.rotate(-90.0f, m.getLeftAxis());
		m.translate(pos);
	};

	Vector3 aimPos, aimDir;

	if (bInVehicle && !bHasWeapon)
	{
		aimPos = Vector3();
		aimDir = Helpers::GetCamera().lookDir;
	}
	else
	{
		bool bHasCrosshair = weaponHandler.GetLocalWeaponAim(aimPos, aimDir);

		if (!bHasCrosshair)
		{
			return;
		}
	}

	Matrix4 overlayTransform;

	Vector3 targetPos = aimPos + aimDir * c_UIOverlayDistance->Value();

	Vector3 hmdPos = vr->GetHMDTransform(true) * Vector3(0.0f, 0.0f, 0.0f);

	overlayTransform.translate(targetPos);
	overlayTransform.lookAt(hmdPos, Vector3(0.0f, 0.0f, 1.0f));
	
	fixupRotation(overlayTransform, targetPos);

	vr->SetCrosshairTransform(overlayTransform);
	overlayTransform.identity();

	short zoom = Helpers::GetInputData().zoomLevel;

	Vector3 upDir = Vector3(0.0f, 0.0f, 1.0f);

	bool bHasScope = (zoom != -1) && weaponHandler.GetLocalWeaponScope(aimPos, aimDir, upDir);

	if (!bHasScope)
	{
		SetScopeTransform(overlayTransform, false);
		return;
	}

	overlayTransform.translate(aimPos);
	overlayTransform.lookAt(aimPos - aimDir, upDir);

	fixupRotation(overlayTransform, aimPos);

	SetScopeTransform(overlayTransform, true);
}

void Game::SetScopeTransform(Matrix4& newTransform, bool bIsVisible)
{
	if (!bIsVisible)
	{
		return;
	}

	Vector3 scopeUp = newTransform.getForwardAxis();
	Vector3 scopeFacing = -newTransform.getLeftAxis();

	Vector3 pos = (newTransform * Vector3(0.0f, 0.0f, 0.0f)) * MetresToWorld(1.0f) + Helpers::GetCamera().position;
	Matrix3 rot;
	Vector2 size(1.0f, 0.75f);
	size *= MetresToWorld(GetScopeSize());

	newTransform.translate(-pos);
	newTransform.rotate(90.0f, newTransform.getLeftAxis());
	newTransform.rotate(-90.0f, newTransform.getUpAxis());
	newTransform.rotate(-90.0f, newTransform.getLeftAxis());

	for (int i = 0; i < 3; i++)
	{
		rot.setColumn(i, &newTransform.get()[i * 4]);
	}

	inGameRenderer.DrawPolygon(pos, scopeFacing, scopeUp, 32, MetresToWorld(GetScopeSize() * 0.5f), D3DCOLOR_ARGB(0, 0, 0, 0), false);

	float SCOPE_DEPTH = 2.0f;
	float SCOPE_INNER_SCALE = 80.0f;

	pos = pos - scopeFacing * MetresToWorld(SCOPE_DEPTH);
	size *= SCOPE_INNER_SCALE;

	inGameRenderer.DrawRenderTarget(vr->GetScopeTexture(), pos, rot, size, false, true);
}

void Game::StoreRenderTargets()
{
	for (int i = 0; i < 8; i++)
	{
		gameRenderTargets[i].width = Helpers::GetRenderTargets()[i].width;
		gameRenderTargets[i].height = Helpers::GetRenderTargets()[i].height;
		gameRenderTargets[i].format = Helpers::GetRenderTargets()[i].format;
		gameRenderTargets[i].renderSurface = Helpers::GetRenderTargets()[i].renderSurface;
		gameRenderTargets[i].renderTexture = Helpers::GetRenderTargets()[i].renderTexture;
	}
}

void Game::RestoreRenderTargets()
{
	for (int i = 0; i < 8; i++)
	{
		Helpers::GetRenderTargets()[i].width = gameRenderTargets[i].width;
		Helpers::GetRenderTargets()[i].height = gameRenderTargets[i].height;
		Helpers::GetRenderTargets()[i].format = gameRenderTargets[i].format;
		Helpers::GetRenderTargets()[i].renderSurface = gameRenderTargets[i].renderSurface;
		Helpers::GetRenderTargets()[i].renderTexture = gameRenderTargets[i].renderTexture;
	}
}

void Game::CreateTextureAndSurface(UINT Width, UINT Height, DWORD Usage, D3DFORMAT Format, IDirect3DSurface9** OutSurface, IDirect3DTexture9** OutTexture)
{
	HRESULT result = Helpers::GetDirect3DDevice9()->CreateTexture(Width, Height, 1, Usage, Format, D3DPOOL_DEFAULT, OutTexture, nullptr);
	if (FAILED(result))
	{
		Logger::err << "[DX9] Failed to create game texture: " << result << std::endl;
		return;
	}

	result = (*OutTexture)->GetSurfaceLevel(0, OutSurface);
	if (FAILED(result))
	{
		Logger::err << "[DX9] Failed to retrieve game surface: " << result << std::endl;
		return;
	}
}
