#include "WorldSimApi.h"
#include "AirBlueprintLib.h"
#include "common/common_utils/Utils.hpp"
#include "Weather/WeatherLib.h"

WorldSimApi::WorldSimApi(ASimModeBase* simmode)
    : simmode_(simmode)
{
}

bool WorldSimApi::loadLevel(const std::string& level_name)
{


	bool success;
	UAirBlueprintLib::RunCommandOnGameThread([this, level_name, &success]() {
		if (simmode_->current_level_) {
			simmode_->current_level_->SetShouldBeLoaded(false);
		}
		else { 
			UE_LOG(LogTemp, Warning, TEXT("your junk is bunk"));
		}
		simmode_->current_level_ = ULevelStreamingDynamic::LoadLevelInstance(
			simmode_->GetWorld(), FString(level_name.c_str()), FVector(0, 0, 0), FRotator(0, 0, 0), success);
	});
	

	return success;
}



void WorldSimApi::spawnObject(const std::string& object_name, const std::string& load_object, const WorldSimApi::Pose& pose)
{
	FARFilter Filter;
	Filter.ClassNames.Add(UStaticMesh::StaticClass()->GetFName());
	Filter.PackagePaths.Add("/Game");
	Filter.PackagePaths.Add("/Airsim");
	Filter.bRecursivePaths = true;

	FActorSpawnParameters new_actor_spawn_params; // new
	new_actor_spawn_params.Name = FName(object_name.c_str()); // new
	FTransform actor_transform = simmode_->getGlobalNedTransform().fromGlobalNed(pose);

	TArray<FAssetData> AssetData;
	UAirBlueprintLib::RunCommandOnGameThread([this, object_name, load_object, Filter, &AssetData, pose, &new_actor_spawn_params, &actor_transform]() {
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().GetAssets(Filter, AssetData);
		for (auto asset : AssetData)
		{
			if (asset.AssetName == FName(load_object.c_str()))
			{
				UStaticMesh* LoadObject = dynamic_cast<UStaticMesh*>(asset.FastGetAsset());
				if (LoadObject)
				{
					AActor* NewActor = simmode_->GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, new_actor_spawn_params); // new
					UStaticMeshComponent* ObjectComponent = NewObject<UStaticMeshComponent>(NewActor);
					ObjectComponent->SetStaticMesh(LoadObject);
					ObjectComponent->SetRelativeLocation(FVector(0, 0, 0));
					ObjectComponent->SetHiddenInGame(false, true);
					ObjectComponent->RegisterComponent();
					NewActor->SetRootComponent(ObjectComponent);
					NewActor->SetActorLocationAndRotation(actor_transform.GetLocation(), actor_transform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
				}
				break;
			}
		}
	});

}


bool WorldSimApi::isPaused() const
{
    return simmode_->isPaused();
}

void WorldSimApi::reset()
{
    UAirBlueprintLib::RunCommandOnGameThread([this]() {
        simmode_->reset();

        //reset any chars we have
        for (auto& c : chars_)
            c.second->reset();
    }, true);
}

void WorldSimApi::pause(bool is_paused)
{
    simmode_->pause(is_paused);
}

void WorldSimApi::continueForTime(double seconds)
{
    simmode_->continueForTime(seconds);
}

void WorldSimApi::setTimeOfDay(bool is_enabled, const std::string& start_datetime, bool is_start_datetime_dst,
    float celestial_clock_speed, float update_interval_secs, bool move_sun)
{
    simmode_->setTimeOfDay(is_enabled, start_datetime, is_start_datetime_dst,
        celestial_clock_speed, update_interval_secs, move_sun);
}

bool WorldSimApi::setSegmentationObjectID(const std::string& mesh_name, int object_id, bool is_name_regex)
{
    bool success;
    UAirBlueprintLib::RunCommandOnGameThread([mesh_name, object_id, is_name_regex, &success]() {
        success = UAirBlueprintLib::SetMeshStencilID(mesh_name, object_id, is_name_regex);
    }, true);
    return success;
}

int WorldSimApi::getSegmentationObjectID(const std::string& mesh_name) const
{
    int result;
    UAirBlueprintLib::RunCommandOnGameThread([&mesh_name, &result]() {
        result = UAirBlueprintLib::GetMeshStencilID(mesh_name);
    }, true);
    return result;
}

void WorldSimApi::printLogMessage(const std::string& message,
    const std::string& message_param, unsigned char severity)
{
    UAirBlueprintLib::LogMessageString(message, message_param, static_cast<LogDebugLevel>(severity));
}

std::vector<std::string> WorldSimApi::listSceneObjects(const std::string& name_regex) const
{
    std::vector<std::string> result;
    UAirBlueprintLib::RunCommandOnGameThread([this, &name_regex, &result]() {
        result = UAirBlueprintLib::ListMatchingActors(simmode_, name_regex);
    }, true);
    return result;
}


WorldSimApi::Pose WorldSimApi::getObjectPose(const std::string& object_name) const
{
    Pose result;
    UAirBlueprintLib::RunCommandOnGameThread([this, &object_name, &result]() {
        AActor* actor = UAirBlueprintLib::FindActor<AActor>(simmode_, FString(object_name.c_str()));
        result = actor ? simmode_->getGlobalNedTransform().toGlobalNed(FTransform(actor->GetActorRotation(), actor->GetActorLocation()))
            : Pose::nanPose();
    }, true);
    return result;
}

bool WorldSimApi::setObjectPose(const std::string& object_name, const WorldSimApi::Pose& pose, bool teleport)
{
    bool result;
    UAirBlueprintLib::RunCommandOnGameThread([this, &object_name, &pose, teleport, &result]() {
        FTransform actor_transform = simmode_->getGlobalNedTransform().fromGlobalNed(pose);
        AActor* actor = UAirBlueprintLib::FindActor<AActor>(simmode_, FString(object_name.c_str()));
        if (actor) {
            if (teleport) 
                result = actor->SetActorLocationAndRotation(actor_transform.GetLocation(), actor_transform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
            else
                result = actor->SetActorLocationAndRotation(actor_transform.GetLocation(), actor_transform.GetRotation(), true);
        }
        else
            result = false;
    }, true);
    return result;
}

void WorldSimApi::enableWeather(bool enable)
{
    UWeatherLib::setWeatherEnabled(simmode_->GetWorld(), enable);
}
void WorldSimApi::setWeatherParameter(WeatherParameter param, float val)
{
    unsigned char param_n = static_cast<unsigned char>(msr::airlib::Utils::toNumeric<WeatherParameter>(param));
    EWeatherParamScalar param_e = msr::airlib::Utils::toEnum<EWeatherParamScalar>(param_n);

    UWeatherLib::setWeatherParamScalar(simmode_->GetWorld(), param_e, val);
}


//------------------------------------------------- Char APIs -----------------------------------------------------------/

void WorldSimApi::charSetFaceExpression(const std::string& expression_name, float value, const std::string& character_name)
{
    AAirSimCharacter* character = getAirSimCharacter(character_name);
    character->setFaceExpression(expression_name, value);
}

float WorldSimApi::charGetFaceExpression(const std::string& expression_name, const std::string& character_name) const
{
    const AAirSimCharacter* character = getAirSimCharacter(character_name);
    return character->getFaceExpression(expression_name);
}

std::vector<std::string> WorldSimApi::charGetAvailableFaceExpressions()
{
    const AAirSimCharacter* character = getAirSimCharacter("");
    return character->getAvailableFaceExpressions();
}

void WorldSimApi::charSetSkinDarkness(float value, const std::string& character_name)
{
    AAirSimCharacter* character = getAirSimCharacter(character_name);
    character->setSkinDarkness(value);
}

float WorldSimApi::charGetSkinDarkness(const std::string& character_name) const
{
    const AAirSimCharacter* character = getAirSimCharacter(character_name);
    return character->getSkinDarkness();
}

void WorldSimApi::charSetSkinAgeing(float value, const std::string& character_name)
{
    AAirSimCharacter* character = getAirSimCharacter(character_name);
    character->setSkinAgeing(value);
}

float WorldSimApi::charGetSkinAgeing(const std::string& character_name) const
{
    const AAirSimCharacter* character = getAirSimCharacter(character_name);
    return character->getSkinAgeing();
}

void WorldSimApi::charSetHeadRotation(const msr::airlib::Quaternionr& q, const std::string& character_name)
{
    AAirSimCharacter* character = getAirSimCharacter(character_name);
    character->setHeadRotation(q);
}

msr::airlib::Quaternionr WorldSimApi::charGetHeadRotation(const std::string& character_name) const
{
    const AAirSimCharacter* character = getAirSimCharacter(character_name);
    return character->getHeadRotation();
}

void WorldSimApi::charSetBonePose(const std::string& bone_name, const msr::airlib::Pose& pose, const std::string& character_name)
{
    AAirSimCharacter* character = getAirSimCharacter(character_name);
    character->setBonePose(bone_name, pose);
}

msr::airlib::Pose WorldSimApi::charGetBonePose(const std::string& bone_name, const std::string& character_name) const
{
    const AAirSimCharacter* character = getAirSimCharacter(character_name);
    return character->getBonePose(bone_name);
}

void WorldSimApi::charResetBonePose(const std::string& bone_name, const std::string& character_name)
{
    AAirSimCharacter* character = getAirSimCharacter(character_name);
    character->resetBonePose(bone_name);
}

void WorldSimApi::charSetFacePreset(const std::string& preset_name, float value, const std::string& character_name)
{
    AAirSimCharacter* character = getAirSimCharacter(character_name);
    character->setFacePreset(preset_name, value);
}

void WorldSimApi::charSetFacePresets(const std::unordered_map<std::string, float>& presets, const std::string& character_name)
{
    AAirSimCharacter* character = getAirSimCharacter(character_name);
    character->setFacePresets(presets);
}
void WorldSimApi::charSetBonePoses(const std::unordered_map<std::string, msr::airlib::Pose>& poses, const std::string& character_name)
{
    AAirSimCharacter* character = getAirSimCharacter(character_name);
    character->setBonePoses(poses);
}
std::unordered_map<std::string, msr::airlib::Pose> WorldSimApi::charGetBonePoses(const std::vector<std::string>& bone_names, const std::string& character_name) const
{
    const AAirSimCharacter* character = getAirSimCharacter(character_name);
    return character->getBonePoses(bone_names);
}

AAirSimCharacter* WorldSimApi::getAirSimCharacter(const std::string& character_name)
{
    AAirSimCharacter* character = nullptr;
    UAirBlueprintLib::RunCommandOnGameThread([this, &character_name, &character]() {
        if (chars_.size() == 0) { //not found in the cache
            TArray<AActor*> characters;
            UAirBlueprintLib::FindAllActor<AAirSimCharacter>(simmode_, characters);
            for (AActor* actor : characters) {
                character = static_cast<AAirSimCharacter*>(actor);
                chars_[std::string(
                    TCHAR_TO_UTF8(*character->GetName()))] = character;
            }
        }

        if (chars_.size() == 0) {
            throw std::invalid_argument(
                "There were no actors of class ACharactor found in the environment");
        }

        //choose first character if name was blank or find by name
        character = character_name == "" ? chars_.begin()->second
            : common_utils::Utils::findOrDefault(chars_, character_name);

        if (!character) {
            throw std::invalid_argument(common_utils::Utils::stringf(
                "Character with name %s was not found in the environment", character_name.c_str()).c_str());
        }
    }, true);

    return character;
}

const AAirSimCharacter* WorldSimApi::getAirSimCharacter(const std::string& character_name) const
{
    return const_cast<WorldSimApi*>(this)->getAirSimCharacter(character_name);
}
//------------------------------------------------- Char APIs -----------------------------------------------------------/

