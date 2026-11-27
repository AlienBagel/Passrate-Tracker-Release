// MOD SUBMISSION DISCLAIMER
/*

The previous version of this mod was rejected with the reason "vibecoded".

To avoid this version being rejected for the same reason, I have:

* Chosen to "source" different parts of code (ei say "I learned this / know how to code this from x example / docs")
* Chosen to do all my small commits on the release repo instead of on my private copy, so me iterating the code is visible
* Chosen to add notes about design choices as the defend the fact that I made them


Additionally, here are a few notes about my code that I thought was important, since I thought they might have been red flags as to it being AI, though I wouldn't know.

	For when I comment	// I like to put them on the same line (mostly) as I find it more readable and concise (LLM's also do this a lot to my knowledge)
	
	-------------------- -------------------- I like to add lines of dashes like this to split up the code. I learned this from Code Bullet I believe :) -------------------- --------------------
	
	I keep the entire mod in 1 file since I hate working with header files in c++, and it's a small mod
	
	If anything seems like it wouldn't have been written in an IDE like that: that's because it wasn't! I write code in Notepad++ because I'm used to it, it has Scintilla (which is a godsend), and I hate the friction that comes with setting up projects in common IDE's (even if they come with smart features like autocomplete)
	

If anything I hope the history of commits that will now be visible should speak to the mod being human made.


*/


// Hmm
#include <Geode/Geode.hpp>

// Other stuff
#include <cmath>
#include <algorithm>
#include <cvolton.level-id-api/EditorIDs.hpp>
#include <fmt/chrono.h>

using namespace geode::prelude;

// Decayed number of passes and fails
struct Rate {
	float a = 0.0;   // Decayed number of passes
	float b = 0.0;   // Decayed number of fails

	// Add a new datapoint using lambda (old data gets weighted down)
	void add(bool pass, float lambda) {
		a *= lambda;              
		b *= lambda;
		if (pass) a += 1.0;       
		else      b += 1.0;
	}
	float total() const    { return a + b; }
	// Passrate
	float estimate() const { return total() > 0.0 ? a / total() : 0.0; }
};

// A slow and a fast counter with change detection.
struct SegmentTracker {
	Rate slow, fast;                 
	// Amount of attempt memory is: 1/(1-lambda)
	inline static float lambdaSlow  = 0.995;    // Memory of ~200 attempts
	inline static float lambdaFast  = 0.95;     // Memory of ~20 attempts
	inline static float floorN = 1.0;       	// How much rates get downsized when "reset"
	
	// Which run it is, start and end percents
	float from = 0;
	float to = 100;

	void add(bool pass) {
		slow.add(pass, lambdaSlow);
		fast.add(pass, lambdaFast);
		
		// Limit memory to 1.5f times the "max". Relevant for when lambda is lowered so it takes less time to adjust.
		float k = 1.f/(1.f-lambdaSlow);
		if(slow.a > k*1.5f)
		{
			slow.a *= k*1.5f/slow.a;
			slow.b *= k*1.5f/slow.b;			
		}
		k = 1.f/(1.f-lambdaFast);
		if(fast.a > k*1.5f)
		{
			fast.a *= k*1.5f/fast.a;
			fast.b *= k*1.5f/fast.b;
		}
	}
	
	// Cut down memory so new attempts weigh more again
	void resetMemory()
	{
		if (slow.total() > floorN) {
			// Becomes the equivalent of having done only floorN attempts, meaning new attemps get more impact
			float k = floorN / slow.total();   
			slow.a *= k;
			slow.b *= k;
			k = floorN / fast.total();
			fast.a *= k;
			fast.b *= k;
		}
	}
	

	float passrate() const { return slow.estimate(); }		// Passrate to display
	
	// Trend indicator calculation
	std::string trend() const 
	{
		float diff = fast.estimate() - slow.estimate();
		if(diff > 0.05) return "increasing";
		else if (diff < -0.05) return "decreasing";
		else return "steady";
	}
	
};

template<>
struct matjson::Serialize<SegmentTracker> {
	static Result<SegmentTracker> fromJson(matjson::Value const& value) {
		GEODE_UNWRAP_INTO(double from, value["from"].asDouble());
		GEODE_UNWRAP_INTO(double to, value["to"].asDouble());
		GEODE_UNWRAP_INTO(double slowPasses, value["slowPasses"].asDouble());
		GEODE_UNWRAP_INTO(double slowFails, value["slowFails"].asDouble());
		GEODE_UNWRAP_INTO(double fastPasses, value["fastPasses"].asDouble());
		GEODE_UNWRAP_INTO(double fastFails, value["fastFails"].asDouble());
		return Ok(SegmentTracker{
			.slow = { .a = static_cast<float>(slowPasses), .b = static_cast<float>(slowFails) }, .fast = {.a = static_cast<float>(fastPasses), .b = static_cast<float>(fastFails) }, 
			.from = static_cast<float>(from), .to = static_cast<float>(to) 
		});
	}

	static matjson::Value toJson(SegmentTracker const& s) {
		auto obj = matjson::Value();
		obj["from"] = s.from;
		obj["to"] = s.to;
		obj["slowPasses"] = s.slow.a;
		obj["slowFails"] = s.slow.b;
		obj["fastPasses"] = s.fast.a;
		obj["fastFails"] = s.fast.b;
		return obj;
	}
};




// PLAYLAYER -------------------- PLAYLAYER -------------------- PLAYLAYER -------------------- PLAYLAYER -------------------- PLAYLAYER -------------------- 
#include <Geode/modify/PlayLayer.hpp>
class $modify(PassratePlayLayer, PlayLayer) {
	struct Fields {
		CCLabelBMFont* label = nullptr;			// Label that shows passrate while playing
		std::vector<float> startPosX; 			// Collected during load
		std::vector<float> startPosPercents;	// Sorted in ascending order, calculated using startPosX
		float startPercent = -1.f;
		float target = 100.f;
		bool attemptActive = false;				// Set to false to avoid "deaths" during run switching
		bool reachedTarget = false;
		bool usingCustomTarget = false;
		
		std::vector<SegmentTracker> segments;										// Segments that get saved
		// Give initial value so that currentSegment doesn't start out undefined (causes crash)
		SegmentTracker customSegment = SegmentTracker{ .from = 0.f, .to = 100.f };	// Where to keep the segment data for a run that's not valid for saving
		SegmentTracker* currentSegment = &customSegment;							// Current run tracking, valid for saving or not
		
		// Get number from settings to it gets saved, explained in info that more info is avaliable from within level
		float lambdaSlow = geode::Mod::get()->getSettingValue<float>("lambda-slow");
		float lambdaFast = geode::Mod::get()->getSettingValue<float>("lambda-fast");
		
		// For grace period
		float attemptTime = 0.f;
		float gracePeriod = Mod::get()->getSettingValue<float>("grace-period");
		
		float customEndingPercent = Mod::get()->getSettingValue<float>("custom-ending-percent");
		
	};

	// Get start position object positions when objects get loaded in
	void addObject(GameObject* obj) {
		if (obj->m_objectID == 31) {
			m_fields->startPosX.push_back(obj->getPositionX());
		}
		
		// ORIGINAL FUNCTION CALL
		PlayLayer::addObject(obj);
	}

	// Calculate startPosPercents, has to be done here and not in init, idk why
	void createObjectsFromSetupFinished() {
		// ORIGINAL FUNCTION CALL
		PlayLayer::createObjectsFromSetupFinished();
	
		auto f = m_fields.self();
		f->startPosPercents.clear();
		for (float x : f->startPosX) {
			f->startPosPercents.push_back(x / m_levelLength * 100.f);
		}
		std::sort(f->startPosPercents.begin(), f->startPosPercents.end());
			
		// Load saved data
		loadSegments();
		
		// Make sure old segments (from old start positions) get deleted properly
		checkSegmentValidity();
		
		// Update lambda in SegmentTracker struct
		updateLambda();

	}

	bool init(GJGameLevel* level, bool dontCreateObjects) {
		// ORIGINAL FUNCTION CALL
		if (!PlayLayer::init(level, dontCreateObjects)) return false;

		// Define label that shows info in bottom left
		auto f = m_fields.self();
		f->label = CCLabelBMFont::create("", "bigFont.fnt");
		f->label->setAnchorPoint({0.f, 0.f});
		f->label->setScale(0.4f);
		f->label->setPosition({5.f, 5.f});
		f->label->setID("passrate-label"_spr);
		f->label->setString("entering level");
		this->addChild(f->label, 100);
		
		// Add event listeners to react to keypresses on this play layer
		this->addEventListener(
			KeybindSettingPressedEventV3(Mod::get(), "next-target-key"),
			[this](Keybind const& keybind, bool down, bool repeat, double timestamp) {
				if (down && !repeat) changeTargetstartPos(true);
			}
		);
		this->addEventListener(
			KeybindSettingPressedEventV3(Mod::get(), "prev-target-key"),
			[this](Keybind const& keybind, bool down, bool repeat, double timestamp) {
				if (down && !repeat) changeTargetstartPos(false);
			}
		);
		
		
		return true;
	}

	void resetLevel() {
		// ORIGINAL FUNCTION CALL
		PlayLayer::resetLevel();
		
		auto f = m_fields.self();

		// If neccesary, set new start percent and reset
		float start = getCurrentPercent();
		if (std::abs(start - f->startPercent) > 0.01f) {
			f->startPercent = start;
			if(!f->usingCustomTarget) f->target = -1; // So updateTarget always changes it to next start pos when we change start percent
			updateTarget();
		}
		
		
		// Update passrate on death
		if (f->attemptActive) {
			// NEW new system	
			if(f->attemptTime >= f->gracePeriod && geode::Mod::get()->getSettingValue<bool>("enable-tracker"))	// Grace period check
				updateActiveSegment(f->reachedTarget);		
		}
		
		f->attemptTime = 0;
		
		f->attemptActive = true;
		f->reachedTarget = false;
		updateLabel();
		
		// Save data every attempt for now, should be cheap
		if(geode::Mod::get()->getSettingValue<bool>("enable-tracker"))
			saveSegments();
	}
	
	// Return pointer to active segment
	SegmentTracker* getActiveSegment()
	{
		auto f = m_fields.self();
		
		SegmentTracker* knownSegment = nullptr;
		
		if(f->usingCustomTarget) goto custom_segment;
		
		// Get active known segment from vector if there
		for (auto& seg : f->segments) {
			if (std::abs(seg.from - f->startPercent) < 0.01f && std::abs(seg.to - f->target) < 0.01f) 
			{
				// return &seg;
				knownSegment = &seg;
			}
		}

		// See if it matches a start pos - start pos pair (or 0 or 100%)
		for(auto p : f->startPosPercents)
		{
			if(std::abs(p - f->startPercent) < 0.01f || std::abs(0.f - f->startPercent) < 0.01f)
			{	// Found a macthing start position for run beginning
				auto p2 = getNextTarget();
				if(std::abs(p2 - f->target) < 0.01f) // Don't check against 100% since getNextTarget can already output that and it would add 0-100 as a segment
				{	// Next position / the ending is a match
					if(knownSegment == nullptr)	// This is a new segment that needs to be added
					{
						f->segments.push_back(SegmentTracker{ .from = f->startPercent, .to = f->target });
						return &f->segments.back();
					}
					else	// We have a known segment, and it is still valid (adjacent)
					{
						return knownSegment;
					}
				}
			}
		}
		
		if(knownSegment != nullptr)	// We found a known segment, but it didn't pass the adjacent start pos check. We need to remove it.
		{	// We can be confident the pointer points to a struct in the vector and that the vector isn't empty.
			std::ptrdiff_t index = knownSegment - f->segments.data(); 		// Find index by subtracting pointer to the first object from our own pointer. This automatically computes the iterator
			f->segments.erase(f->segments.begin() + index);					// We add our index to segments.begin() to get the correct position in the vector and remove it
			
			// currentSegment might have been messed with if it pointed to something in the vector. Just completely 
			// reasign it, at worst the passrate gets reset for this new custom segment which is fine
			f->customSegment = SegmentTracker{ .from = f->startPercent, .to = f->target };
			return &f->customSegment;
			
		}
			
		// No matching start position.
		custom_segment:
		
		if (std::abs(f->currentSegment->from - f->startPercent) < 0.01f && std::abs(f->currentSegment->to - f->target) < 0.01f) 
		{
			// We stayed on the same custom segment, just return itself
			return f->currentSegment;
		}
		
		// We must be on a new custom segment. Return address to a new struct
		f->customSegment = SegmentTracker{ .from = f->startPercent, .to = f->target };
		return &f->customSegment;
	}
	
	// Runs through all segments, which will delete old invalid ones.
	void checkSegmentValidity()
	{
		auto f = m_fields.self();
		float savedStart = f->startPercent;
		float savedTarget = f->target;

		auto copy = f->segments;	// Copy segments since we delete from it in getActiveSegment()
		for (auto const& seg : copy) {
			f->startPercent = seg.from;
			f->target = seg.to;
			getActiveSegment();
		}

		f->startPercent = savedStart;
		f->target = savedTarget;
	}
	
	void updateActiveSegment(bool pass)
	{
		auto f = m_fields.self();
		
		f->currentSegment->add(pass);
	}
	
	void updateLambda()
	{
		auto f = m_fields.self();
		SegmentTracker::lambdaSlow = f->lambdaSlow;
		SegmentTracker::lambdaFast = f->lambdaFast;
	}

	// Just for if we reached the target yet
	void postUpdate(float dt) {
		// ORIGINAL FUNCTION CALL
		PlayLayer::postUpdate(dt);
		
		auto f = m_fields.self();
		if (f->attemptActive) {
			if(getCurrentPercent() >= f->target)
			{
				f->reachedTarget = true;
			}
			else if(std::abs(f->target - 100.f) < 0.01f && getCurrentPercent() >= f->customEndingPercent) // Check against custom ending percent
			{
				f->reachedTarget = true;
			}			
		}
		
		f->attemptTime += dt;
		
		// Make sure label updates when entering level:
		if (!f->label) return;
		
		if(strcmp(f->label->getString(), "entering level") == 0) 
		{
			updateLabel();
		}
	}

	// Get start postion (or 100%) that is next after our current percent
	float getNextTarget() { 
		auto f = m_fields.self();
		for(int i = 0; i < f->startPosPercents.size(); i++)
		{
			if(f->startPosPercents[i] > f->startPercent)
			{
				return f->startPosPercents[i];
			}
		}
		return 100.f;
	}

	// Cycle backwards/forwards through startpos as targets. Passing true moves forward
	void changeTargetstartPos(bool forward)
	{
		auto f = m_fields.self();
		
		if(f->usingCustomTarget) return;
		
		if(forward)
		{
			// Set target to next startposition in front of target (or 100% when there are no more start positions)
			for(int i = 0; i < f->startPosPercents.size(); i++)
			{
				if(f->startPosPercents[i] > f->target)
				{
					f->target = f->startPosPercents[i];
					f->attemptActive = false;
					break;
				}
				if(i == f->startPosPercents.size()-1) f->target = 100;
				f->attemptActive = false;
			}
		}
		else
		{
			// Go through start pos, set target to first start pos before current target
			// We don't worry about going too far, gets handled by updateTarget
			for(int i = f->startPosPercents.size()-1; i > -1; i--)
			{
				if(f->startPosPercents[i] < f->target)
				{
					f->attemptActive = false;
					f->target = f->startPosPercents[i];
					break;
				}
			}
		}
		
		// Update target in case it was placed behind current start percent
		updateTarget();
		updateLabel();
	}
	
	void applyCustomTarget(bool enabled, float percent)
	{
		auto f = m_fields.self();
		
		f->usingCustomTarget = enabled;
		if(!enabled) { f->target = getNextTarget(); f->attemptActive = false; }
		updateTarget(percent);
		updateLabel();
	}

	// Sets a specific target if specified and updates target so it isn't behind current start percent (using getNextTarget)
	void updateTarget(float customTarget = -1)
	{
		auto f = m_fields.self();
		
		// Custom target set?
		if(f->usingCustomTarget && customTarget != -1)
		{
			f->target = customTarget;
		}
		else
		// Do we need new automatic target?
		if(f->target <= f->startPercent && !f->usingCustomTarget)
		{
			f->target = getNextTarget();
			f->attemptActive = false;
		}
		
		// Update the current segment
		f->currentSegment = getActiveSegment();
	}
	

	// Update the UI text
	void updateLabel() { 
		auto f = m_fields.self();

		if (!f->label) return;
		
		if(!geode::Mod::get()->getSettingValue<bool>("enable-tracker")) 
		{
			f->label->setString("");
			return;
		}
		
		// New label for new system
		f->label->setString(
			fmt::format(
				"{:.1f}-{:.1f}%: {:.1f}% {}",
				f->startPercent, f->target, f->currentSegment->passrate() * 100.f, 
				Mod::get()->getSettingValue<bool>("lambda-fast-text") ? fmt::format("({})", f->currentSegment->trend()) : ""
			).c_str()
		);
		
		f->label->setOpacity(Mod::get()->getSettingValue<int>("text-opacity"));
	}

	// In own function so I know exactly how it's being calculated
	float getCurrentPercent()
	{
		return m_player1->getPositionX() / m_levelLength * 100.f;
	}
	

	// Saving and loading functions
	std::string saveKey() {
		if (m_level->m_levelType == GJLevelType::Editor) {
			return fmt::format("editor-{}", EditorIDs::getID(m_level));
		}
		return fmt::format("level-{}", m_level->m_levelID.value());
	}
	void loadSegments() {
		m_fields->segments = Mod::get()->getSavedValue<std::vector<SegmentTracker>>(saveKey());
	}
	void saveSegments() {
		Mod::get()->setSavedValue(saveKey(), m_fields->segments);
	}
	
	
	// For data deletion and whitelisting
	bool isWhitelisted(std::string const& key) {
		auto list = Mod::get()->getSavedValue<std::vector<std::string>>("whitelist");
		return std::ranges::find(list, key) != list.end();	// Find returns list.end() when it doesn't find it
	}
	void setWhitelisted(std::string const& key, bool on) {
		auto list = Mod::get()->getSavedValue<std::vector<std::string>>("whitelist");
		auto it = std::ranges::find(list, key);
		if (on && it == list.end()) list.push_back(key);	// We want to whitelist it and it isn't already
		if (!on && it != list.end()) list.erase(it);		// We don't want to whitelist it and we need to remove it
		Mod::get()->setSavedValue("whitelist", list);
	}
	
		
	
};

void deleteNonWhitelisted() {
	auto mod = Mod::get();
	auto whitelist = mod->getSavedValue<std::vector<std::string>>("whitelist");
	auto& container = mod->getSaveContainer();

	// Collect first, erase after: erasing while iterating invalidates the loop
	std::vector<std::string> toDelete;
	for (auto& entry : container) {
		auto key = entry.getKey();
		if (!key) continue;
		bool isLevelData = key->starts_with("level-") || key->starts_with("editor-");
		if (isLevelData && std::ranges::find(whitelist, *key) == whitelist.end()) {
			toDelete.push_back(*key);
		}
	}
	for (auto const& key : toDelete) container.erase(key);
}

// NEW POPUP SYSTEM -------------------- NEW POPUP SYSTEM -------------------- NEW POPUP SYSTEM -------------------- NEW POPUP SYSTEM -------------------- NEW POPUP SYSTEM -------------------- 

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/modify/PauseLayer.hpp>

class PassrateHubPopup;

// Parses a percent string (decimal can be comma or period). Returns nullopt if invalid.
std::optional<float> parsePercent(std::string text) 
{
	std::replace(text.begin(), text.end(), ',', '.');
	auto parsed = utils::numFromString<float>(text);
	if (!parsed.isOk()) return std::nullopt;
	return std::clamp(parsed.unwrap(), 0.f, 100.f);
}

float parseNumberUnclamped(std::string text)
{
	std::replace(text.begin(), text.end(), ',', '.');
	auto parsed = utils::numFromString<float>(text);
	if (!parsed.isOk()) return 0;
	return parsed.unwrap();
}

// Doesn't need it's own function anymore, but is fine as is
std::string previewFor(float percent) 
{
	return fmt::format("Runs will be set to end at {:.2f} until disabled", percent);
}

// -------------------- -------------------- -------------------- -------------------- --------------------
// Base class for all sub-popups. Holds the hub and play layer pointers.
// -------------------- -------------------- -------------------- -------------------- --------------------
class PassrateSubPopup : public geode::Popup {
protected:
	PassrateHubPopup* m_hub = nullptr;
	PassratePlayLayer* m_pl = nullptr;

	bool initBase(PassrateHubPopup* hub, PassratePlayLayer* pl, float width, float height, char const* title) {
		if (!Popup::init(width, height)) return false;
		this->setTitle(title);
		m_hub = hub;
		m_pl = pl;
		return true;
	}
	
	virtual void onClosedSpecial() {};

public:
	// Defined after PassrateHubPopup, since it needs the full hub type.
	void onClose(CCObject* sender) override;
};

// -------------------- -------------------- -------------------- -------------------- --------------------
// Custom Target sub-popup
// -------------------- -------------------- -------------------- -------------------- --------------------
class CustomTargetPopup : public PassrateSubPopup {
protected:
	TextInput* m_input = nullptr;
	CCMenuItemToggler* m_toggle = nullptr;
	CCLabelBMFont* m_preview = nullptr;
	
	bool enabled;

	bool init(PassrateHubPopup* hub, PassratePlayLayer* pl) {
		if (!initBase(hub, pl, 240.f, 170.f, "Custom Target")) return false;

		auto f = pl->m_fields.self();
		enabled = f->usingCustomTarget;
		float percent = f->target;

		// Number input
		m_input = TextInput::create(100.f, "0 - 100", "bigFont.fnt");
		m_input->setFilter("0123456789.,");
		m_input->setMaxCharCount(6);
		m_input->setString(enabled ? fmt::format("{:.2f}", percent) : "");
		m_mainLayer->addChildAtPosition(m_input, Anchor::Center, {0.f, 30.f});

		// Checkbox + label
		m_toggle = CCMenuItemToggler::createWithStandardSprites(
			this, menu_selector(CustomTargetPopup::onToggle), 0.7f
		);
		m_toggle->toggle(enabled);
		m_buttonMenu->addChildAtPosition(m_toggle, Anchor::Center, {-80.f, -2.f});

		auto toggleLabel = CCLabelBMFont::create("Use custom target", "bigFont.fnt");
		toggleLabel->setScale(0.4f);
		toggleLabel->setAnchorPoint({0.f, 0.5f});
		m_mainLayer->addChildAtPosition(toggleLabel, Anchor::Center, {-62.f, -2.f});

		// Live preview text
		m_preview = CCLabelBMFont::create("", "bigFont.fnt");
		m_preview->setScale(0.25f);
		m_mainLayer->addChildAtPosition(m_preview, Anchor::Center, {0.f, -25.f});

		// Set the callback last, so it can't fire before m_preview exists
		m_input->setCallback([this](std::string const&) { updatePreview(); });

		updatePreview();
		return true;
	}
	
	void onClosedSpecial() override
	{
		bool on = m_toggle->isToggled();
		float value = 0.f;

		if (on) {
			auto parsed = parsePercent(m_input->getString());
			if (!parsed) {
				Notification::create("Invalid percentage", NotificationIcon::Error)->show();
				return;
			}
			value = *parsed;
		}
		
		m_pl->applyCustomTarget(on, value);
	}

	void updatePreview() {
		if (!enabled) {
			m_preview->setString("Custom target will not be set");
			return;
		}
		auto value = parsePercent(m_input->getString());
		m_preview->setString(value ? previewFor(*value).c_str() : "");
	}

	void onToggle(CCObject*) {
		enabled = !enabled;	// We used enabled here since the toggle state isn't updated until after this function or something
		updatePreview();
	}

public:
	static CustomTargetPopup* create(PassrateHubPopup* hub, PassratePlayLayer* pl) {
		auto ret = new CustomTargetPopup();
		if (ret->init(hub, pl)) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}
};

// -------------------- -------------------- -------------------- -------------------- --------------------
// Settings sub-popup
// -------------------- -------------------- -------------------- -------------------- --------------------
// ** Grace period
// ** Custom ending percent
// ** Sensitivity, slow and fast rates
// ** Other stuff also :)
class SettingsPopup : public PassrateSubPopup {
protected:
	
	TextInput* m_inputGracePeriod = nullptr;
	TextInput* m_inputEndingPercent = nullptr;
	TextInput* m_inputSlow = nullptr;
	TextInput* m_inputFast = nullptr;
	TextInput* m_inputOpacity = nullptr;
	CCMenuItemToggler* m_toggle = nullptr;
	
	CCLabelBMFont* m_previewSlow = nullptr;
	CCLabelBMFont* m_previewFast = nullptr;
	
	PassratePlayLayer* Pl;

	bool init(PassrateHubPopup* hub, PassratePlayLayer* pl) {
		if (!initBase(hub, pl, 240.f, 280.f, "Settings")) return false;
		
		Pl = pl;
		auto f = Pl->m_fields.self();
		bool enabled = geode::Mod::get()->getSettingValue<bool>("lambda-fast-text");
		
		constexpr float offset = 55.f; 
		
		// Label
		auto m_labelGracePeriod = CCLabelBMFont::create("Grace period", "bigFont.fnt");
		m_labelGracePeriod->setScale(0.4f);
		m_labelGracePeriod->setAnchorPoint({0.5f, 0.5f});
		m_mainLayer->addChildAtPosition(m_labelGracePeriod, Anchor::Center, {0.f, 45.f+offset});
		
		// Grace period input
		m_inputGracePeriod = TextInput::create(100.f, "0 - 4", "bigFont.fnt");
		m_inputGracePeriod->setFilter("0123456789.,");
		m_inputGracePeriod->setMaxCharCount(6);
		m_inputGracePeriod->setScale(0.5f);
		m_inputGracePeriod->setString(fmt::format("{:.2f}", f->gracePeriod));
		m_mainLayer->addChildAtPosition(m_inputGracePeriod, Anchor::Center, {0.f, 30.f+offset});
		
		// Label
		auto m_labelEndingPercent = CCLabelBMFont::create("Custom ending percent", "bigFont.fnt");
		m_labelEndingPercent->setScale(0.4f);
		m_labelEndingPercent->setAnchorPoint({0.5f, 0.5f});
		m_mainLayer->addChildAtPosition(m_labelEndingPercent, Anchor::Center, {0.f, 10.f+offset});
		
		// Custom ending percent input
		m_inputEndingPercent = TextInput::create(100.f, "50 - 100", "bigFont.fnt");
		m_inputEndingPercent->setFilter("0123456789.,");
		m_inputEndingPercent->setMaxCharCount(7);
		m_inputEndingPercent->setScale(0.5f);
		m_inputEndingPercent->setString(fmt::format("{:.2f}", f->customEndingPercent));
		m_mainLayer->addChildAtPosition(m_inputEndingPercent, Anchor::Center, {0.f, -5.f+offset});
		
		// Label
		auto m_labelSlow = CCLabelBMFont::create("Passrate sensitivity", "bigFont.fnt");
		m_labelSlow->setScale(0.4f);
		m_labelSlow->setAnchorPoint({0.5f, 0.5f});
		m_mainLayer->addChildAtPosition(m_labelSlow, Anchor::Center, {0.f, -25.f+offset});
		
		// Sensitivity slow, input
		m_inputSlow = TextInput::create(100.f, "0 - 1", "bigFont.fnt");
		m_inputSlow->setFilter("0123456789.,");
		m_inputSlow->setMaxCharCount(7);
		m_inputSlow->setScale(0.5f);
		m_inputSlow->setString(fmt::format("{:.4f}", f->lambdaSlow));
		m_mainLayer->addChildAtPosition(m_inputSlow, Anchor::Center, {0.f, -40.f+offset});
		
		// Live preview text
		m_previewSlow = CCLabelBMFont::create("", "bigFont.fnt");
		m_previewSlow->setScale(0.25f);
		m_previewSlow->setWidth(480.f);  // Alignment only applies within a set width :(
		m_previewSlow->setAlignment(CCTextAlignment::kCCTextAlignmentCenter);
		m_mainLayer->addChildAtPosition(m_previewSlow, Anchor::Center, {0.f, -55.f+offset});
		
		// Label
		auto m_labelFast = CCLabelBMFont::create("Passrate trend sensitivity", "bigFont.fnt");
		m_labelFast->setScale(0.4f);
		m_labelFast->setAnchorPoint({0.5f, 0.5f});
		m_mainLayer->addChildAtPosition(m_labelFast, Anchor::Center, {0.f, -80.f+offset});
		
		// Sensitivity fast, input
		m_inputFast = TextInput::create(100.f, "0 - 1", "bigFont.fnt");
		m_inputFast->setFilter("0123456789.,");
		m_inputFast->setMaxCharCount(7);
		m_inputFast->setScale(0.5f);
		m_inputFast->setString(fmt::format("{:.4f}", f->lambdaFast));
		m_mainLayer->addChildAtPosition(m_inputFast, Anchor::Center, {0.f, -95.f+offset});
		
		// Live preview text
		m_previewFast = CCLabelBMFont::create("", "bigFont.fnt");
		m_previewFast->setScale(0.25f);
		m_previewFast->setWidth(480.f);  // Alignment only applies within a set width :(
		m_previewFast->setAlignment(CCTextAlignment::kCCTextAlignmentCenter);
		m_mainLayer->addChildAtPosition(m_previewFast, Anchor::Center, {0.f, -110.f+offset});
		
		// Show trend toggle
		m_toggle = CCMenuItemToggler::createWithStandardSprites(
			this, menu_selector(SettingsPopup::onToggle), 0.7f
		);
		m_toggle->toggle(enabled);
		m_buttonMenu->addChildAtPosition(m_toggle, Anchor::Center, {-80.f, -135.f+offset});

		auto toggleLabel = CCLabelBMFont::create("Show trend indicator", "bigFont.fnt");
		toggleLabel->setScale(0.4f);
		toggleLabel->setAnchorPoint({0.f, 0.5f});
		m_mainLayer->addChildAtPosition(toggleLabel, Anchor::Center, {-62.f, -135.f+offset});
		
		// Label
		auto m_labelOpacity = CCLabelBMFont::create("Text opacity (0-255)", "bigFont.fnt");
		m_labelOpacity->setScale(0.4f);
		m_labelOpacity->setAnchorPoint({0.5f, 0.5f});
		m_mainLayer->addChildAtPosition(m_labelOpacity, Anchor::Center, {0.f, -160.f+offset});
		
		// Text opacity in level
		m_inputOpacity = TextInput::create(100.f, "0 - 1", "bigFont.fnt");
		m_inputOpacity->setFilter("0123456789");
		m_inputOpacity->setMaxCharCount(6);
		m_inputOpacity->setScale(0.5f);
		m_inputOpacity->setString(fmt::format("{}", geode::Mod::get()->getSettingValue<int>("text-opacity")));
		m_mainLayer->addChildAtPosition(m_inputOpacity, Anchor::Center, {0.f, -175.f+offset});
		
		// Number inputs update preview text
		m_inputSlow->setCallback([this](std::string const&) { updatePreview(); });
		m_inputFast->setCallback([this](std::string const&) { updatePreview(); });
		
		updatePreview();
		
		return true;
	}
	
	// Save new settings
	void onClosedSpecial() override
	{	
		auto f = Pl->m_fields.self();
		
		f->gracePeriod = std::clamp(parseNumberUnclamped(m_inputGracePeriod->getString()), 0.f, 4.f);
		geode::Mod::get()->setSettingValue<float>("grace-period", f->gracePeriod);
		
		f->customEndingPercent = std::clamp(parseNumberUnclamped(m_inputEndingPercent->getString()), 50.f, 100.f);
		geode::Mod::get()->setSettingValue<float>("custom-ending-percent", f->customEndingPercent);
		
		f->lambdaSlow = std::clamp(parseNumberUnclamped(m_inputSlow->getString()), 0.1f, 1.f);
		geode::Mod::get()->setSettingValue<float>("lambda-slow", f->lambdaSlow);
		
		f->lambdaFast = std::clamp(parseNumberUnclamped(m_inputFast->getString()), 0.1f, 1.f);
		geode::Mod::get()->setSettingValue<float>("lambda-fast", f->lambdaFast);
		
		Pl->updateLambda();
		
		geode::Mod::get()->setSettingValue<bool>("lambda-fast-text", m_toggle->isToggled());
		
		geode::Mod::get()->setSettingValue<int>("text-opacity",  std::clamp(static_cast<int>(parseNumberUnclamped(m_inputOpacity->getString())), 0, 255));
		
		Pl->updateLabel();
	}

	// For preview text about lambda values. Could be 2 functions but is fine as is
	void updatePreview() {
		m_previewSlow->setString(fmt::format("The passrate will be based on roughly\nthe last {:.0f} attempts", 
		1.f/(1-std::clamp(parseNumberUnclamped(m_inputSlow->getString()), 0.1f, 1.f))).c_str());
		m_previewFast->setString(fmt::format("The trend indicator will be based on roughly\nthe last {:.0f} attempts", 
		1.f/(1-std::clamp(parseNumberUnclamped(m_inputFast->getString()), 0.1f, 1.f))).c_str());
	}
	
	// Doesn't need to do anything atm
	void onToggle(CCObject*) {	}

public:
	static SettingsPopup* create(PassrateHubPopup* hub, PassratePlayLayer* pl) {
		auto ret = new SettingsPopup();
		if (ret->init(hub, pl)) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}
};

// -------------------- -------------------- -------------------- -------------------- --------------------
// Info sub-popup
// -------------------- -------------------- -------------------- -------------------- --------------------
// ** Expected attempts (median)
// ** Passrate per part needed to beat under 1000 attempts from 0
class InfoPopup : public PassrateSubPopup {
protected:
	bool init(PassrateHubPopup* hub, PassratePlayLayer* pl) {
		if (!initBase(hub, pl, 480.f, 170.f, "Info")) return false;
		
		auto f = pl->m_fields.self();
		
		// Expected attempts
		CCLabelBMFont* m_expectedAttempts = nullptr;
		m_expectedAttempts = CCLabelBMFont::create("", "bigFont.fnt");
		m_expectedAttempts->setScale(0.35f);
		m_expectedAttempts->setWidth(480.f);  // Alignment only applies within a set width :(
		m_expectedAttempts->setAlignment(CCTextAlignment::kCCTextAlignmentCenter);
		m_mainLayer->addChildAtPosition(m_expectedAttempts, Anchor::Center, {0.f, 25.f});
		
		// Expected attempts calculation
		float p = 1.f;
		for(auto segment : f->segments)
		{
			p *= segment.passrate();
		}
		float expectedAttempts = -1.f/log2(1.f-p);	// Division by zero leads to -inf / inf which is fine here
		
		m_expectedAttempts->setString(f->startPosPercents.size()+1 <= f->segments.size() 
			? fmt::format("There is a 50% chance (with current passrates) that you\n will have beaten the level from 0 after {:.0f} attempts!\n{}{}", expectedAttempts, 
			expectedAttempts>100000 ? "Yikes!" : "", 	// Call out people with low passrates so they know they need to lock in
			std::isinf(expectedAttempts) ? " (Passrates at 0% cause problems for the calculation)" : "").c_str()
			: "Needs data for all segments");
		
		// Passrate needed
		CCLabelBMFont* m_passrateNeeded = nullptr;
		m_passrateNeeded = CCLabelBMFont::create("", "bigFont.fnt");
		m_passrateNeeded->setScale(0.35f);
		m_passrateNeeded->setWidth(480.f);  // Alignment only applies within a set width :(
		m_passrateNeeded->setAlignment(CCTextAlignment::kCCTextAlignmentCenter);
		m_mainLayer->addChildAtPosition(m_passrateNeeded, Anchor::Center, {0.f, -25.f});
		
		// Passrate needed calculation
		float passrateNeeded = -1.f;
		if(f->segments.size() > 0)
			passrateNeeded = std::pow(1.f/1000.f, 1.f/f->segments.size());
				
		m_passrateNeeded->setString(f->startPosPercents.size()+1 <= f->segments.size()	// Only have enough data when segment size is 1+startpospercents size
			? fmt::format("To beat this level on average every 1000 attempts from 0\n each segment needs a passrate of at least {:.1f}%!", passrateNeeded*100.f).c_str() 
			: "Needs data for all segments");
		
		return true;
	}

public:
	static InfoPopup* create(PassrateHubPopup* hub, PassratePlayLayer* pl) {
		auto ret = new InfoPopup();
		if (ret->init(hub, pl)) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}
};

// -------------------- -------------------- -------------------- -------------------- --------------------
// Hub popup: a list of buttons that open the sub-popups
// -------------------- -------------------- -------------------- -------------------- --------------------
class PassrateHubPopup : public geode::Popup {
protected:
	PassratePlayLayer* m_pl = nullptr;

	bool whitelistedEnabled;
	bool trackerEnabled;
	

	bool init(PassratePlayLayer* pl) 
	{
		if (!Popup::init(240.f, 240.f)) return false;
		this->setTitle("Passrate Tracker");
		m_pl = pl;
		auto f = m_pl->m_fields.self();
		
		constexpr float offset = 50.f;
		
		whitelistedEnabled =  m_pl->isWhitelisted(pl->saveKey());
		trackerEnabled = geode::Mod::get()->getSettingValue<bool>("enable-tracker");
		
		// Got tired of naming variables, future me don't judge okay, btnSpr3 is very readable
		auto btnSpr = ButtonSprite::create("Custom Target");
		btnSpr->setScale(0.7f);
		auto targetButton = CCMenuItemSpriteExtra::create(
		   btnSpr, this,
			menu_selector(PassrateHubPopup::onCustomTarget)
		);
		m_buttonMenu->addChildAtPosition(targetButton, Anchor::Center, {0.f, 20.f+offset});
		
		auto btnSpr1 = ButtonSprite::create("Settings");
		btnSpr1->setScale(0.7f);
		auto settingsButton = CCMenuItemSpriteExtra::create(
			btnSpr1, this,
			menu_selector(PassrateHubPopup::onSettings)
		);
		m_buttonMenu->addChildAtPosition(settingsButton, Anchor::Center, {0.f, -10.f+offset});

		auto btnSpr2 = ButtonSprite::create("Info");
		btnSpr2->setScale(0.7f);
		auto infoButton = CCMenuItemSpriteExtra::create(
			btnSpr2, this,
			menu_selector(PassrateHubPopup::onInfo)
		);
		m_buttonMenu->addChildAtPosition(infoButton, Anchor::Center, {0.f, -40.f+offset});
		
		auto btnSpr3 = ButtonSprite::create("Reset Segment");
		btnSpr3->setScale(0.7f);
		auto resetMemoryButton = CCMenuItemSpriteExtra::create(
			btnSpr3, this,
			menu_selector(PassrateHubPopup::onResetSegment)
		);
		m_buttonMenu->addChildAtPosition(resetMemoryButton, Anchor::Center, {0.f, -70.f+offset});
		
		// Enable passrate tracker toggle
		auto m_toggleEnabled = CCMenuItemToggler::createWithStandardSprites(
			this, menu_selector(PassrateHubPopup::onToggleEnabled), 0.7f
		);
		m_toggleEnabled->toggle(geode::Mod::get()->getSettingValue<bool>("enable-tracker"));
		m_buttonMenu->addChildAtPosition(m_toggleEnabled, Anchor::Center, {-85.f, -100.f+offset});

		auto toggleLabelEnabled = CCLabelBMFont::create("Enable passrate tracker", "bigFont.fnt");
		toggleLabelEnabled->setScale(0.4f);
		toggleLabelEnabled->setAnchorPoint({0.f, 0.5f});
		m_mainLayer->addChildAtPosition(toggleLabelEnabled, Anchor::Center, {-67.f, -100.f+offset});
		
		
		// Whitelist level toggle
		auto m_toggleWhitelist = CCMenuItemToggler::createWithStandardSprites(
			this, menu_selector(PassrateHubPopup::onToggleWhitelist), 0.7f
		);
		m_toggleWhitelist->toggle(whitelistedEnabled);
		m_buttonMenu->addChildAtPosition(m_toggleWhitelist, Anchor::Center, {-85.f, -130.f+offset});

		auto toggleLabelWhitelist = CCLabelBMFont::create("Whitelist level", "bigFont.fnt");
		toggleLabelWhitelist->setScale(0.4f);
		toggleLabelWhitelist->setAnchorPoint({0.f, 0.5f});
		m_mainLayer->addChildAtPosition(toggleLabelWhitelist, Anchor::Center, {-67.f, -130.f+offset});

		return true;
	}

	void openChild(geode::Popup* child) 
	{
		m_buttonMenu->setEnabled(false); // Freeze the hub while the child is open
		child->show();
	}

	void onCustomTarget(CCObject*) 
	{
		openChild(CustomTargetPopup::create(this, m_pl));
	}

	void onSettings(CCObject*) 
	{
		openChild(SettingsPopup::create(this, m_pl));
	}

	void onInfo(CCObject*) 
	{
		openChild(InfoPopup::create(this, m_pl));
	}
	
	void onResetSegment(CCObject*)
	{
		auto f = m_pl->m_fields.self();
		
		f->currentSegment->resetMemory();
	}
	
	void onToggleWhitelist(CCObject*)
	{
		whitelistedEnabled = !whitelistedEnabled;
		m_pl->setWhitelisted(m_pl->saveKey(), whitelistedEnabled);
	}
	
	void onToggleEnabled(CCObject*)
	{
		trackerEnabled = !trackerEnabled;
		auto f = m_pl->m_fields.self();
		geode::Mod::get()->setSettingValue<bool>("enable-tracker", trackerEnabled);
		m_pl->updateLabel();
	}

public:
	// Called by a sub-popup when it closes
	void onChildClosed() {
		m_buttonMenu->setEnabled(true);
	}

	static PassrateHubPopup* create(PassratePlayLayer* pl) {
		auto ret = new PassrateHubPopup();
		if (ret->init(pl)) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}
};

// Defined here because it needs the full PassrateHubPopup type
void PassrateSubPopup::onClose(CCObject* sender) {
	onClosedSpecial();
	if (m_hub) m_hub->onChildClosed();
	geode::Popup::onClose(sender);
}

// -------------------- -------------------- -------------------- -------------------- --------------------
// Pause menu button opens the hub
// -------------------- -------------------- -------------------- -------------------- --------------------
class $modify(PassratePauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		// Node IDs moved the menu under main-layer, older versions had it directly on the layer. Check both to be sure
		CCNode* menu = nullptr;
		if (auto mainLayer = this->getChildByID("main-layer")) {
			menu = mainLayer->getChildByID("left-button-menu");
		}
		if (!menu) menu = this->getChildByID("left-button-menu");
		if (!menu) return;

		auto label = CCLabelBMFont::create("%", "bigFont.fnt");
		auto sprite = CircleButtonSprite::create(label);
		sprite->setScale(0.75f);
		auto button = CCMenuItemSpriteExtra::create(
			sprite, this, menu_selector(PassratePauseLayer::onCustomTarget)
		);
		button->setID("custom-target-button"_spr);

		menu->addChild(button);
		menu->updateLayout();
	}

	void onCustomTarget(CCObject*) {
		auto pl = static_cast<PassratePlayLayer*>(PlayLayer::get());
		if (!pl) return;
		PassrateHubPopup::create(pl)->show();
	}
};

#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <asp/fs.hpp>
$on_mod(Loaded) {
	// Button in settings (built in setting type) (from https://github.com/geode-sdk/geode/pull/1903)
	// Delete non whitelisted data button in settings:
	ButtonSettingPressedEventV3(geode::Mod::get(), "prune-data").listen([] (auto buttonKey) {
		if (buttonKey == "delete-button") {
			createQuickPopup(
				"Delete data",
				"Delete saved data for every level that is <cr>not</c> whitelisted? "
				"This cannot be undone.",
				"Cancel", "Delete",
				[](auto, bool confirmed) {
					if (confirmed) deleteNonWhitelisted();
				}
			);
		}
	}).leak();
	
	// (Updated for v6) Simple backup system. asp::fs functions don't throw, so if anything fails just skip the backup
	auto saveDir = Mod::get()->getSaveDir();
	auto src = saveDir / "saved.json";
	if (!src.exists()) return;

	auto backupDir = saveDir / "backups";
	if (asp::fs::createDirAll(backupDir).isErr()) return;

	auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
	auto dest = backupDir / fmt::format("saved-{:%Y-%m-%d_%H-%M-%S}.json", now);

	// Read and write instead of copying file, don't know how to copy, should be fine
	auto contents = asp::fs::readToString(src);
	if (!contents) return;
	if (!asp::fs::write(dest, contents.unwrap())) return;

	// Keep only the newest N: timestamped names sort oldest-first
	auto iterator = asp::fs::iterDir(backupDir);
	if (!iterator) return;

	std::vector<asp::fs::Path> backups;
	for (auto result : iterator.unwrap()) {
		if (!result) continue;	// Skip entries that can't be read
		auto& entry = result.unwrap();
		if (entry.isFile()) backups.push_back(entry.path);
	}

	std::sort(backups.begin(), backups.end(), [](auto const& a, auto const& b) {
		return a.string() < b.string();
	});

	constexpr size_t keep = 10;
	for (size_t i = 0; i + keep < backups.size(); i++) {
		(void)asp::fs::remove(backups[i]);	// Failing to delete an old backup isn't worth reacting to
	}
}
