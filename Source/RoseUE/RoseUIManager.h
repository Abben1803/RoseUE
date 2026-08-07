// URoseUIManager — hosts every classic-client window (SRoseUIWindow over
// Content/UI/Layouts/*.json) for ARoseCharacter.  Feature files (RoseUIHud,
// RoseUIMinimap, RoseUISkills, RoseUIChat, RoseUIMisc) register window defs in
// their Register() hook (called from BeginPlay); the manager owns open/close,
// viewport anchoring, toggle keys and the cursor input mode.  The generic
// path (`RoseUI <dialog>` on the possessed pawn) opens ANY converted layout
// as a plain draggable window, registered or not.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "RoseUIManager.generated.h"

class ARoseCharacter;
class SRoseUIWindow;
class SWidget;
class UInputComponent;

// Which viewport corner a window's Offset is measured from (windows stay
// draggable — dragging mutates the offset, the anchor stays).
enum class ERoseUIAnchor : uint8 { TopLeft, TopRight, BottomLeft, BottomRight, Center };

struct FRoseUIWindowDef
{
	FString Dialog;                            // Content/UI/Layouts/<Dialog>.json
	FVector2D Offset = FVector2D(200, 200);    // from the anchor corner
	ERoseUIAnchor Anchor = ERoseUIAnchor::TopLeft;
	FKey ToggleKey;                            // unset = no key (menu/console only)
	bool bOpenAtStart = false;                 // HUD elements
	int32 ZOrder = 9;
	// Optional overlay covering the whole window (position children absolutely
	// at the layout's authored coordinates) — rebuilt on every Open.
	TFunction<TSharedPtr<SWidget>(class URoseUIManager&)> BuildContent;
	// Optional handler for labeled layout buttons (close buttons close instead).
	TFunction<void(class URoseUIManager&, const FString&)> OnButton;
};

UCLASS(ClassGroup = (Rose))
class ROSEUE_API URoseUIManager : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Host a LAYOUT-DRIVEN window: no SRoseModernWindow chrome (the dialog
	 *  draws its own frame, caption and close, and drags itself). */
	void ToggleLayoutWindow(FName Id, TSharedRef<SWidget> Content, TSharedPtr<SWidget>& Root);

	/** Close a window by id — public because layout-driven windows fire their
	 *  own close button and have no SRoseModernWindow to route through. */
	void CloseModernWindow(FName Id);

	URoseUIManager();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// Build the HUD + windows for the local player's pawn.  Safe to call more
	// than once — on a network client the owning pawn's Controller replicates
	// AFTER BeginPlay, so ARoseCharacter::EnsureLocalSetup calls this again once
	// the pawn actually knows it is locally controlled.
	void EnsureInit();
	bool bInitialized = false;

	// Feature files call this from their Register() hook (BeginPlay order).
	void RegisterWindow(FName Id, FRoseUIWindowDef Def);

	void Toggle(FName Id);
	void Open(FName Id);
	void Close(FName Id);

	// Modern draggable overlay windows (custom Slate, not classic dialogs).  The
	// skill panel (K) and skill tree (X) are toggled from the HUD hamburger and
	// keys bound on the character.
	void ToggleSkillPanel();
	void ToggleSkillTree();
	bool IsSkillPanelOpen() const { return SkillPanelRoot.IsValid(); }
	bool IsSkillTreeOpen() const { return SkillTreeRoot.IsValid(); }

	// Focus the always-on modern chat input (Enter key / HUD "Chat" button).
	void FocusChat();
	// Show/hide the always-on modern minimap (M key / HUD "Map" button).
	void ToggleMinimap();
	// Modern options/system window (Z key / HUD "System" button).
	void ToggleOptions();
	// Modern quest journal (Q key / HUD "Quest" button).
	void ToggleQuestJournal();
	// DEV item spawner (F10 / `RoseDev` console) — search + click-to-give.
	void ToggleGripTuner();
	void ToggleDevSpawn();
	bool IsQuestJournalOpen() const { return QuestJournalRoot.IsValid(); }

	// NPC conversation window (click a town NPC): runs the CON script through
	// FRoseDialogSession (RoseDialog.cpp) and shows SRoseDialogWindow
	// (RoseUIDialog.cpp).  One conversation at a time, like the client.
	void OpenNpcDialog(class ARoseNpc* Npc);
	void CloseNpcDialog();
	bool IsNpcDialogOpen() const { return DialogRoot.IsValid(); }
	/** The NPC the open conversation belongs to, or null.  Lets the character
	 *  close the window when the player walks away from them. */
	class ARoseNpc* GetOpenDialogNpc() const
	{ return DialogRoot.IsValid() ? DialogNpc.Get() : nullptr; }

	// NPC store window (GF_openStore from a CON script — RoseUIStore.cpp).
	void OpenStore(class ARoseNpc* Npc);
	void CloseStore();
	bool IsOpen(FName Id) const { return OpenWindows.Contains(Id); }
	// Close+reopen preserving the dragged position (icon refresh — NEVER
	// rebuild a widget from inside its own click handler; defer via a flag).
	void Reopen(FName Id);

	// The open window's Slate widget (tab queries etc.), or null.
	TSharedPtr<SRoseUIWindow> GetWindow(FName Id) const;

	// Bind every registered def's ToggleKey; safe to call before/after
	// registration (late defs bind as they register).
	void SetupInput(UInputComponent* Input);

	// `RoseUI <dialog>`: toggle any layout by name as a bare window.
	void ToggleGeneric(const FString& Dialog);

	// Cursor policy: ROSE always shows the cursor (point-and-click MMO); once
	// the HUD registers, this stays true.  ARoseCharacter::UpdateUIInputMode
	// consults it.
	bool WantsCursor() const { return bHasHUD || OpenWindows.Num() > 0 || SkillPanelRoot.IsValid() || SkillTreeRoot.IsValid(); }

	ARoseCharacter* GetRoseCharacter() const;

private:
	struct FOpenWindow
	{
		TSharedPtr<SRoseUIWindow> Window;
		TSharedPtr<SWidget> Root;
	};

	TMap<FName, FRoseUIWindowDef> Defs;
	TMap<FName, FOpenWindow> OpenWindows;
	UPROPERTY() UInputComponent* BoundInput = nullptr;
	TSet<FName> BoundKeys;
	bool bHasHUD = false;
	// Remembered user drag per window, so a close+reopen keeps the moved spot.
	TMap<FName, FVector2D> DragMemory;
	// The modern always-on HUD (added to the viewport for the local player).
	TSharedPtr<class SWidget> HudRoot;
	// Modern draggable overlay windows while open (null = closed): the outer
	// positioning canvas + the chrome widget (for its dragged Position).
	TSharedPtr<class SWidget> SkillPanelRoot;
	TSharedPtr<class SRoseModernWindow> SkillPanelWindow;
	TSharedPtr<class SWidget> SkillTreeRoot;
	TSharedPtr<class SRoseModernWindow> SkillTreeWindow;
	TSharedPtr<class SWidget> OptionsRoot;
	TSharedPtr<class SRoseModernWindow> OptionsWindow;
	TSharedPtr<class SWidget> QuestJournalRoot;
	TSharedPtr<class SRoseModernWindow> QuestJournalWindow;
	TSharedPtr<class SWidget> GripTunerRoot;
	TSharedPtr<class SRoseModernWindow> GripTunerWindow;
	TSharedPtr<class SWidget> DevSpawnRoot;
	TSharedPtr<class SRoseModernWindow> DevSpawnWindow;
	// Always-on modern chat overlay (bottom-left), created for the local player.
	TSharedPtr<class SWidget> ChatRoot;
	// Modern minimap overlay (top-right); toggled by M.
	TSharedPtr<class SWidget> MinimapRoot;
	// NPC conversation overlay (bottom-centre) + its running session.
	TSharedPtr<class SWidget> DialogRoot;
	TWeakObjectPtr<class ARoseNpc> DialogNpc;   // who the open dialog is with
	TSharedPtr<class FRoseDialogSession> ActiveDialog;
	// NPC store overlay (centre).
	TSharedPtr<class SWidget> StoreRoot;

	// Shared open/close for a modern overlay window (drag-position remembered).
	void ToggleModernWindow(FName Id, const FText& Title, float Width,
		TSharedRef<SWidget> Content, TSharedPtr<SWidget>& Root,
		TSharedPtr<class SRoseModernWindow>& Window);



	void BindToggleKey(FName Id, const FKey& Key);
	void UpdateInputMode();
	// True if the dialog's converted layout root has DEFAULT_VISIBLE set (the
	// client's InitInterfacePos auto-show rule).
	static bool IsDefaultVisible(const FString& Dialog);
};

// Modern always-on HUD (RoseHUD.cpp) — created by the manager for the local
// player; replaces the classic hud_info/menu/quickbar windows.
TSharedRef<class SWidget> RoseHUD_Make(URoseUIManager& UI);

// Modern skill window (RoseUISkillPanel.cpp) — created on demand by
// URoseUIManager::ToggleSkillPanel.
TSharedRef<class SWidget> RoseSkillPanel_Make(URoseUIManager& UI);

// Modern skill tree content + its title (RoseUISkills.cpp) — created on demand by
// URoseUIManager::ToggleSkillTree (job-bucket-selected node layout).
TSharedRef<class SWidget> RoseSkillTree_MakeContent(URoseUIManager& UI);
FString RoseSkillTree_Title(URoseUIManager& UI);

// Modern chat overlay (RoseUIChat.cpp) — created always-on by the manager; the
// Enter key focuses its input via RoseChat_FocusInput.
TSharedRef<class SWidget> RoseChat_Make(URoseUIManager& UI);
void RoseChat_FocusInput();

// Modern minimap overlay (RoseUIMinimap.cpp).
TSharedRef<class SWidget> RoseMinimap_Make(URoseUIManager& UI);

// Modern options/system content (RoseUIOptions.cpp).
TSharedRef<class SWidget> RoseOptions_MakeContent(URoseUIManager& UI);

// Modern quest journal content (RoseUIMisc.cpp).
TSharedRef<class SWidget> RoseQuestJournal_MakeContent(URoseUIManager& UI);
TSharedRef<class SWidget> RoseDevSpawn_MakeContent(URoseUIManager& UI);   // RoseUIDev.cpp

// NPC conversation overlay (RoseUIDialog.cpp).
TSharedRef<class SWidget> RoseDialog_Make(URoseUIManager& UI,
	TSharedPtr<class FRoseDialogSession> Session);

// NPC store overlay (RoseUIStore.cpp).
TSharedRef<class SWidget> RoseStore_Make(URoseUIManager& UI, class ARoseNpc* Npc);

// Feature registration hooks — one per feature file; RoseUIManager::BeginPlay
// calls them in this order.  Each file registers its windows + content.
void RoseUIHud_Register(URoseUIManager& UI);
void RoseUIMinimap_Register(URoseUIManager& UI);
void RoseUISkills_Register(URoseUIManager& UI);
void RoseUIChat_Register(URoseUIManager& UI);
void RoseUIMisc_Register(URoseUIManager& UI);
