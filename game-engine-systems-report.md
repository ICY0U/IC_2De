# Game Engine Systems Architecture: A Technical Reference

**Scope:** This document is an internal engineering reference covering the full stack of a modern game engine — editor/tooling ("frontend") and runtime ("backend"). Each section is organized around a general engineering concept, explaining how the mechanism works and why it exists, then grounding that explanation with concrete implementation examples drawn from **Unreal Engine 5.x (C++/Blueprints)** and **Unity (C#, GameObject/MonoBehaviour model and DOTS/Entities ECS)** — used purely as reference material to illustrate real, shipping implementations of each concept, leaning on whichever engine's example is clearest for a given point. Inline citations `[n]` refer to the numbered [References](#references) list, drawn from official Epic Games and Unity Technologies documentation.

**Version baseline:** Unreal Engine 5.8 documentation (current at time of writing; Chaos Physics has been the default physics engine since UE5's initial release, replacing PhysX). Unity 6 / Unity 6000.x LTS documentation, covering both the legacy GameObject/MonoBehaviour stack and the Entities 1.x (DOTS) package. Package and API names drift between versions; where relevant, version-sensitivity is called out explicitly.

---

## Table of Contents

1. [Engine Architecture Overview](#1-engine-architecture-overview)
   - [1.1 Main Loop / Game Loop](#11-main-loop--game-loop)
   - [1.2 Subsystem & Module Layout](#12-subsystem--module-layout)
   - [1.3 ECS vs. OOP/GameObject-Component Models](#13-ecs-vs-oopgameobject-component-models)
   - [1.4 Engine vs. Game Separation](#14-engine-vs-game-separation)
2. [Rendering Pipeline](#2-rendering-pipeline)
   - [2.1 Forward vs. Deferred Rendering](#21-forward-vs-deferred-rendering)
   - [2.2 PBR Materials & Shaders](#22-pbr-materials--shaders)
   - [2.3 Lighting & Global Illumination](#23-lighting--global-illumination)
   - [2.4 Shadows](#24-shadows)
   - [2.5 Post-Processing](#25-post-processing)
3. [Scene Graph / World Representation](#3-scene-graph--world-representation)
   - [3.1 Unreal: World / Level / Actor / Component](#31-unreal-world--level--actor--component)
   - [3.2 Unity: Scene / GameObject / Transform](#32-unity-scene--gameobject--transform)
   - [3.3 World Partition vs. Scene Streaming / Addressables](#33-world-partition-vs-scene-streaming--addressables)
4. [Physics & Collision Systems](#4-physics--collision-systems)
   - [4.1 Unreal: Chaos Physics](#41-unreal-chaos-physics)
   - [4.2 Unity: PhysX, Box2D, and Unity Physics (DOTS)](#42-unity-physx-box2d-and-unity-physics-dots)
   - [4.3 Collision Detection, Rigid Bodies, Constraints](#43-collision-detection-rigid-bodies-constraints)
5. [Animation Systems](#5-animation-systems)
   - [5.1 Skeletal Animation Fundamentals](#51-skeletal-animation-fundamentals)
   - [5.2 Animation Blueprints vs. Mecanim/Animator](#52-animation-blueprints-vs-mecanimanimator)
   - [5.3 Inverse Kinematics](#53-inverse-kinematics)
   - [5.4 Blend Spaces](#54-blend-spaces)
6. [Audio Systems](#6-audio-systems)
   - [6.1 Unreal: MetaSounds & the Audio Engine](#61-unreal-metasounds--the-audio-engine)
   - [6.2 Unity: Audio, FMOD, and Wwise Integration Patterns](#62-unity-audio-fmod-and-wwise-integration-patterns)
   - [6.3 Spatial Audio](#63-spatial-audio)
7. [Input Systems](#7-input-systems)
   - [7.1 Unreal Enhanced Input](#71-unreal-enhanced-input)
   - [7.2 Unity Input System Package](#72-unity-input-system-package)
8. [Scripting & Gameplay Frameworks](#8-scripting--gameplay-frameworks)
   - [8.1 Blueprints vs. C++ and the Gameplay Ability System](#81-blueprints-vs-c-and-the-gameplay-ability-system)
   - [8.2 MonoBehaviour vs. DOTS/Entities](#82-monobehaviour-vs-dotsentities)
9. [UI Systems](#9-ui-systems)
   - [9.1 Unreal: UMG & Slate](#91-unreal-umg--slate)
   - [9.2 Unity: UGUI & UI Toolkit](#92-unity-ugui--ui-toolkit)
10. [Networking & Multiplayer](#10-networking--multiplayer)
    - [10.1 Replication Models & RPCs](#101-replication-models--rpcs)
    - [10.2 Client Prediction & Reconciliation](#102-client-prediction--reconciliation)
    - [10.3 Unreal's Replication Graph](#103-unreals-replication-graph)
    - [10.4 Unity Netcode for GameObjects](#104-unity-netcode-for-gameobjects)
11. [Asset Pipeline & Content Management](#11-asset-pipeline--content-management)
    - [11.1 Import Pipelines](#111-import-pipelines)
    - [11.2 Unreal Asset Registry](#112-unreal-asset-registry)
    - [11.3 Unity AssetDatabase & Addressables](#113-unity-assetdatabase--addressables)
    - [11.4 Cooking & Building](#114-cooking--building)
12. [Memory Management & Performance](#12-memory-management--performance)
    - [12.1 Garbage Collection vs. Manual/Reference-Counted Memory](#121-garbage-collection-vs-manualreference-counted-memory)
    - [12.2 Object Pooling](#122-object-pooling)
    - [12.3 Profiling Tools](#123-profiling-tools)
13. [AI Systems](#13-ai-systems)
    - [13.1 Behavior Trees](#131-behavior-trees)
    - [13.2 NavMesh & Pathfinding](#132-navmesh--pathfinding)
    - [13.3 Unreal Environment Query System (EQS)](#133-unreal-environment-query-system-eqs)
    - [13.4 Unity NavMesh & Behavior Packages](#134-unity-navmesh--behavior-packages)
14. [Save / Serialization Systems](#14-save--serialization-systems)
15. [Build, Deployment & Platform Abstraction Layers](#15-build-deployment--platform-abstraction-layers)
16. [Editor Tooling & Extensibility](#16-editor-tooling--extensibility)
    - [16.1 Unreal Editor Utility Widgets & Python](#161-unreal-editor-utility-widgets--python)
    - [16.2 Unity Editor Scripting & Custom Inspectors](#162-unity-editor-scripting--custom-inspectors)
    - [16.3 Custom Tools & Plugins](#163-custom-tools--plugins)
17. [References](#references)

---

## 1. Engine Architecture Overview

### 1.1 Main Loop / Game Loop

Every real-time engine is structured around a loop that advances simulation time, processes input, updates game logic, and produces a frame: `Input → Fixed/Variable Update → Physics → Animation → Gameplay → Rendering → Present`. The central architectural question a loop design has to answer is how to decouple simulation timestep from render framerate, and how to schedule thousands of per-object update calls each frame without that scheduling becoming a serial bottleneck — the latter matters increasingly as core counts grow and single-threaded update loops become the ceiling on frame time.

Unreal's answer is a dependency-ordered, parallelizable scheduler. `FEngineLoop::Tick()` (engine-level) drives `UGameEngine::Tick()` each frame, and within a `UWorld` the tick phase walks registered actors and components through **Tick Groups** (`TG_PrePhysics`, `TG_StartPhysics`, `TG_DuringPhysics`, `TG_EndPhysics`, `TG_PostPhysics`, `TG_PostUpdateWork`) built on the Task Graph system, so independent ticks execute in parallel rather than serially [59]. Each `AActor` and `UActorComponent` owns a `FTickFunction` (`PrimaryActorTick` / `PrimaryComponentTick`) that can declare explicit tick dependencies (`AddTickPrerequisiteActor`), intervals, and group membership — letting engine code parallelize ticking of unrelated actors while still guaranteeing ordering where it matters, such as physics resolving before animation reads the result [59]:

```cpp
// AActor subclass — engine calls Tick() once per frame by default
void AMyPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Velocity += Acceleration * DeltaSeconds;
    AddActorWorldOffset(Velocity * DeltaSeconds);
}

// Constructor: opt into ticking and declare ordering
AMyPawn::AMyPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
}
```

Unity takes a simpler but less automatically-parallel approach for its default object model: the `PlayerLoop` is a fixed, inspectable sequence of internal phases (`Initialization → FixedUpdate → PreUpdate → Update → PreLateUpdate → PostLateUpdate → ...`), exposed for modification via `UnityEngine.LowLevel.PlayerLoop`. `MonoBehaviour` callbacks map onto this loop directly: `FixedUpdate()` runs at a fixed timestep synchronized with the physics step (the same fixed/variable decoupling Unreal achieves via tick groups, here achieved by a dedicated phase), `Update()` runs once per rendered frame at variable delta time, and `LateUpdate()` runs after all `Update()` calls — used for cases like camera-follow logic that must read a final, already-updated transform:

```csharp
public class Mover : MonoBehaviour
{
    public Vector3 velocity;
    void FixedUpdate()  // fixed timestep, synced with physics
    {
        transform.position += velocity * Time.fixedDeltaTime;
    }
    void LateUpdate()   // after all Updates, e.g. camera follow
    {
        Camera.main.transform.position = transform.position + Vector3.back * 10f;
    }
}
```

Where Unity does get comparable declarative, automatic parallelism to Unreal's tick-group model is in DOTS/Entities: systems run inside `SystemGroup`s (`InitializationSystemGroup`, `SimulationSystemGroup`, `PresentationSystemGroup`) scheduled into the player loop, supporting explicit `[UpdateBefore]`/`[UpdateAfter]` ordering and automatic multithreading via the C# Job System — architecturally a close analog of Unreal's tick-group/Task-Graph scheduler, just opt-in rather than the default execution path for ordinary gameplay code.

### 1.2 Subsystem & Module Layout

Large engines decompose their codebases into modules or subsystems with explicit dependency graphs, both to keep builds parallelizable and to allow runtime feature toggling (headless servers, editor-only tooling, platform-specific code) without recompiling everything that doesn't need it.

Unreal expresses this at the build level through **Modules** — each declaring public/private dependencies, include paths, and defines in a `.Build.cs` file [60]. Modules are categorized as `Runtime`, `Editor`, `Developer`, or `ThirdParty`, and can be `Static`, `DynamicallyLoaded`, or `Loaded` at engine startup. Above modules, UE5 layers the **Subsystem** framework (`UEngineSubsystem`, `UGameInstanceSubsystem`, `UWorldSubsystem`, `ULocalPlayerSubsystem`) — auto-instantiated singletons scoped to a well-defined lifetime, replacing the older pattern of ad-hoc global managers with something the reflection system and garbage collector both understand:

```cpp
// MyModule.Build.cs
public class MyModule : ModuleRules
{
    public MyModule(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore" });
    }
}

// A world-scoped subsystem, auto-created per UWorld
UCLASS()
class UMyWorldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
};
```

Unity gets to a similar place from the opposite direction, because its runtime core is closed-source native code exposed to C# via a scripting API. Modularity here happens at the package and assembly level rather than inside the engine binary itself: reusable engine or game-extension code ships as **UPM packages** (`com.unity.*`), each with a `package.json` declaring `dependencies` and version; within a single project, `.asmdef` (Assembly Definition) files carve C# source into separately compiled assemblies with explicit references, giving the same faster-incremental-compiles-plus-enforced-dependency-direction benefit that Unreal's `Build.cs` graph provides. Unity 6's Entities package extends this with injectable `SystemBase`/`ComponentSystemGroup` registration — conceptually close to Unreal's subsystem model, just scoped to ECS execution rather than general engine services:

```json
// Packages/manifest.json — project-level module dependencies
{
  "dependencies": {
    "com.unity.entities": "1.3.5",
    "com.unity.inputsystem": "1.11.2",
    "com.unity.netcode.gameobjects": "2.1.1"
  }
}
```

### 1.3 ECS vs. OOP/GameObject-Component Models

The two dominant patterns for organizing game-object data are object-oriented composition, where a scene entity is an object holding a heterogeneous list of component objects each with its own methods and virtual dispatch, and Entity-Component-System (ECS), a data-oriented pattern where an entity is just an ID, components are plain data structs, and systems are stateless functions operating over tightly packed arrays ("archetypes"/chunks) of components matching a query. ECS trades OOP's flexibility and locality-of-reasoning for cache-friendly, highly parallelizable batch processing — the difference matters most once object counts climb into the thousands or tens of thousands, where pointer-chasing and virtual dispatch overhead start to dominate frame time.

Both engines default to composition and treat ECS as an opt-in tool for scale. Unreal's default model is `AActor` as a reflected `UObject` container owning `UActorComponent`s (detailed in [Section 3.1](#31-unreal-world--level--actor--component)). It does ship a genuine ECS, **Mass Entity** (part of the "Mass" framework used for large-scale crowds and AI in titles like the Unreal city-sample and Fortnite), built on an archetype/chunk model, but it is used selectively for systems that need to simulate thousands of lightweight agents rather than as the default actor representation:

```cpp
// Mass Entity fragment (plain data) and processor (system), conceptually
// mirroring ECS component/system separation, used selectively for
// high-agent-count simulation (e.g. crowds) rather than as the default model.
USTRUCT()
struct FMassVelocityFragment : public FMassFragment
{
    GENERATED_BODY()
    FVector Value = FVector::ZeroVector;
};

UCLASS()
class UMassMoveProcessor : public UMassProcessor
{
    GENERATED_BODY()
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};
```

Unity ships both models side by side, more explicitly than Unreal does. The default is the **GameObject/MonoBehaviour** model — a `GameObject` is a container of independently heap-allocated, virtual-dispatched `Component` objects (`Transform`, `Rigidbody`, user scripts). The **Entities** package (formerly branded "DOTS", Data-Oriented Technology Stack) implements true archetype-based ECS on top of the same engine: `IComponentData` structs are stored contiguously in memory **chunks** per unique archetype, and `SystemBase`/`ISystem` implementations iterate them via `Entities.ForEach` or `IJobEntity`, JIT/AOT-compiled and vectorized by the **Burst** compiler and parallelized by the **C# Job System** [11][12]:

```csharp
// GameObject/MonoBehaviour model
public class Health : MonoBehaviour
{
    public float current = 100f;
    public void TakeDamage(float amount) => current -= amount;
}

// Entities (DOTS/ECS) model
public struct Health : IComponentData { public float Value; }

public partial struct DamageSystem : ISystem
{
    public void OnUpdate(ref SystemState state)
    {
        foreach (var health in SystemAPI.Query<RefRW<Health>>())
            health.ValueRW.Value -= 1f; // batch-processed over a tightly packed chunk
    }
}
```

The practical pattern in both engines is the same: use composition-style objects for the majority of gameplay content — dozens to low thousands of instances, where designer-friendly per-object inspection and arbitrary attached behavior matter more than raw throughput — and reach for ECS only for the subset of systems (crowds, projectiles, particle-driven gameplay, large open-world agent simulation) that need to scale into the tens of thousands.

### 1.4 Engine vs. Game Separation

Engines separate reusable, project-agnostic runtime and tooling code from game-specific logic and content, typically via a plugin/package boundary plus a strict discipline that engine code never references game code.

In Unreal, that boundary is structural and lives in source: `Engine/` (engine source, versioned separately, not normally modified per project) versus a project's `Source/` and `Content/` directories. Game-specific systems are built as **Plugins** (`.uplugin` plus one or more modules) or as project modules, and engine code never references project types by construction. Epic's own reference game framework, **Lyra**, demonstrates the boundary in practice — its `GameFeatures` plugins, built on the **Game Features and Modular Gameplay** plugin, can be enabled or disabled per-experience without recompiling the engine. Because the engine ships as buildable source, studios routinely fork and patch it directly, which is a major reason AAA studios favor Unreal for deep low-level customization — at the cost of merge overhead when pulling upstream Epic changes.

Unity enforces the same boundary at the binary level instead: the engine is a closed-source native runtime distributed as versioned Editor installs, and all game code is C# compiled against the engine's managed API surface, which makes it structurally incapable of modifying engine internals directly outside of the separately licensed Unity Source Code program. Reusable game-side code is packaged the same way engine features are — as UPM packages — so a studio's shared gameplay framework versions and distributes identically to `com.unity.entities`. This closed-core model enforces a cleaner, more stable API boundary and simpler upgrades than Unreal's fork-and-patch model, but customization below the API surface requires either the Source Code program or waiting on Unity to expose an extension point.

---

## 2. Rendering Pipeline

### 2.1 Forward vs. Deferred Rendering

*Forward rendering* shades each surface fragment against all relevant lights in a single geometry pass. *Deferred rendering* splits this into a geometry pass that writes surface attributes — albedo, normal, roughness, metallic, depth — into a **G-Buffer**, followed by a lighting pass that computes shading per-pixel from the G-Buffer, decoupling light count from geometry complexity. Deferred scales better with many dynamic lights, since lighting cost is a function of screen pixels rather than (lights × triangles); forward is cheaper for transparency, MSAA, and low light counts, and is essentially mandatory for VR and tile-based mobile GPUs where G-Buffer bandwidth is prohibitive.

UE5 defaults to a **deferred shading** renderer (`r.ShadingPath=Deferred`), which is also the pipeline Lumen and Nanite are built against, but also ships a **Forward Shading** renderer (`r.ForwardShading=1`) used primarily for VR and mobile, trading deferred's flexibility for MSAA support and lower bandwidth. Translucent materials always use a forward-style pass even inside the deferred renderer, since a G-Buffer cannot represent overlapping transparent surfaces.

Unity treats rendering path as a per-pipeline configuration choice rather than a single engine-wide switch, which is a direct consequence of shipping multiple Scriptable Render Pipelines: the legacy **Built-In Render Pipeline** and **URP** both support Forward and Deferred rendering paths, selectable in pipeline asset settings, while **HDRP** uses a deferred-first path with a forward path reserved for specific material types such as transparents and anisotropic materials. Unity 6's render pipeline documentation lays out these tradeoffs explicitly per pipeline [19][20][21]:

```csharp
// URP: rendering path is selected on the URP Asset, not per-material
// UniversalRenderPipelineAsset.renderingMode = RenderingMode.Deferred;
```

### 2.2 PBR Materials & Shaders

Physically Based Rendering represents materials via a small set of physically meaningful parameters — base color/albedo, metallic, roughness, normal, ambient occlusion, emissive — evaluated against a microfacet BRDF (commonly Cook-Torrance/GGX), so materials look plausible under any lighting condition without hand-tuned per-light hacks. Both engines converge on the same metallic/roughness parameterization, which is now an industry-standard workflow rather than an engine-specific choice, so content and shader knowledge transfers well between them.

Unreal authors materials as node graphs in the **Material Editor**, compiled to HLSL shader permutations. The default UE5 shading model is `MSM_DefaultLit`, driven by the standard PBR input pins (Base Color, Metallic, Specular, Roughness, Normal, Emissive, Ambient Occlusion). A **Material Instance** system lets artists override scalar/vector/texture parameters of a parent material without recompiling shaders, and **Material Layers**/**Material Functions** provide reusable, composable shading graphs; HLSL can also be authored directly via **Custom** nodes or the newer **Substrate** (Strata) material framework for multi-layer materials:

```cpp
// C++: mutating a Material Instance Dynamic (MID) at runtime
UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
MID->SetScalarParameterValue(TEXT("Roughness"), 0.35f);
MID->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor::Red);
MeshComponent->SetMaterial(0, MID);
```

Unity's PBR path is the **Standard Shader** (Built-in RP) or **Lit Shader** (URP/HDRP), consuming the same albedo/metallic/roughness(smoothness)/normal/AO/emissive parameter set, authorable via **Shader Graph** — node-based, and a close analog of Unreal's Material Editor — or hand-written HLSL using the target Scriptable Render Pipeline's shading library. Because shaders here target one of several pipelines rather than a single first-party renderer, hand-written shaders are not portable between URP/HDRP/Built-in without adaptation, which is the main authoring-ergonomics cost of Unity's pluggable-pipeline architecture relative to Unreal's single renderer. `MaterialPropertyBlock` provides a lightweight way to vary per-renderer parameters without instancing materials, avoiding the draw-call/batching breaks a full material instance would cause:

```csharp
var mpb = new MaterialPropertyBlock();
mpb.SetFloat("_Smoothness", 0.65f);
mpb.SetColor("_BaseColor", Color.red);
renderer.SetPropertyBlock(mpb);
```

### 2.3 Lighting & Global Illumination

Global illumination (GI) accounts for indirect, bounced light. Historically, engines relied on **baked lightmaps** — precomputed static lighting, cheap at runtime, but with no support for dynamic geometry or time-of-day — or light probes for dynamic objects moving through baked scenes. Real-time dynamic GI (voxel cone tracing, screen-space GI, ray/SDF tracing, ReSTIR-style methods) removes the bake step and supports fully dynamic scenes at increased runtime cost, and the industry's center of gravity has been shifting toward dynamic GI as hardware ray-tracing capability has spread.

UE5's headline move in this direction is **Lumen**, the default fully dynamic GI and reflections solution, replacing static lightmap baking as the primary workflow [1]. Lumen combines software and hardware ray tracing, a Surface Cache, and screen-space probes to solve diffuse interreflection with effectively infinite bounces plus glossy/specular reflections, targeting 30–60 fps with 4–8 ms frame budgets on consoles at 1080p [1][2]. Because it updates fully at runtime, time-of-day and destructible geometry work without a rebake, and it scales from millimeter to kilometer environments [2] — at the cost of meaningfully more GPU time than a baked solution, plus its own constraints (it performs best paired with Nanite geometry and specific material setups, and can show light-leaking or temporal lag under heavy dynamic change). UE5 still supports the legacy static **Lightmass** baked-GI pipeline for projects that prioritize baked lightmap quality or performance over full dynamism:

```
// Unreal: enabling Lumen on a project (Project Settings > Rendering)
r.DynamicGlobalIlluminationMethod = 1   // Lumen
r.ReflectionMethod = 1                  // Lumen Reflections
```

Unity's mainstream workflow still leans more on the baked side of this spectrum. Its GI backends are selectable per project: the **Progressive Lightmapper** (CPU or GPU) bakes static/mixed lighting into lightmaps and light probes, and **Adaptive Probe Volumes** (APV) provide baked/dynamic-hybrid indirect lighting for dynamic objects moving through baked scenes — architecturally similar in intent to Unreal's older Lightmass-plus-probe approach. HDRP additionally offers ray-traced GI and reflections on supported hardware, conceptually closer to Lumen but dependent on hardware ray tracing rather than Lumen's hybrid software/hardware approach, and which GI features are available at all depends on the chosen render pipeline [19][20]:

```csharp
// Unity: baking is a project/lighting-settings operation, not runtime code —
// invoked via Lightmapping API for editor tooling/automation
Lightmapping.giWorkflowMode = Lightmapping.GIWorkflowMode.OnDemand;
Lightmapping.Bake();
```

### 2.4 Shadows

Real-time shadows are typically computed via shadow maps — depth rendered from the light's perspective, then sampled and compared during shading — with variants for cascaded directional shadows across view distance, virtual/clipmap shadow maps for large open worlds, and ray-traced shadows on hardware-RT-capable GPUs.

UE5's default high-quality method, paired with Nanite and Lumen, is **Virtual Shadow Maps (VSM)** — a clipmap-based, page-cached virtual texture approach that delivers consistent, high-resolution shadows across huge draw distances without the cascade-seam artifacts of classic Cascaded Shadow Maps (CSM). VSM is architecturally more advanced than CSM (per-page caching that scales to open worlds without manual cascade tuning) but carries a higher baseline cost, and CSM remains available as a lighter-weight legacy option. Unity's shadow system, by contrast, is pipeline-dependent and stays closer to the CSM model throughout: Built-in RP and URP use Cascaded Shadow Maps with configurable cascade counts and distances on the `UniversalRenderPipelineAsset`, requiring manual cascade-distance tuning to avoid visible seams in large open worlds, while HDRP additionally supports **Contact Shadows**, **Micro Shadows**, and hardware ray-traced shadows on supported GPUs — simpler to tune and cheaper on lower-end and mobile hardware than VSM, at the cost of the open-world seam problem VSM was built to solve.

### 2.5 Post-Processing

A post-processing stack applies full-screen effects — tone mapping, bloom, exposure/auto-exposure, color grading, depth of field, motion blur, anti-aliasing, screen-space reflections/AO — as a chain of passes over the rendered frame. Both engines converge on essentially the same authoring pattern here: a spatially-scoped **volume** with blended, priority-ordered overrides, which is a case where industry convention (largely following Unreal's precedent) has become the de facto standard across engines.

Unreal configures post-processing via **Post Process Volumes** placed in the level, each with a blend radius and priority for local overrides, or a global unbound volume, exposing parameters for Bloom, Exposure, Color Grading (with LUT support), Depth of Field (Gaussian, Bokeh, Cinematic), Motion Blur, Chromatic Aberration, Lens Flares, and anti-aliasing method selection — TAA, TSR (Temporal Super Resolution, UE5's default upscaler/AA), or MSAA in forward mode. URP and HDRP use the analogous **Volume** framework — `Volume` components with `VolumeProfile` assets holding override-able effect settings (Bloom, Tonemapping, Color Adjustments, Depth of Field, Motion Blur, Vignette) blended by proximity and priority — while the older Built-in RP instead relies on the separately-installed **Post Processing Stack (v2)** package, a source of practical fragmentation absent from Unreal's single unified implementation across its one renderer:

```csharp
// URP: adjusting a volume profile override at runtime
if (volume.profile.TryGet<Bloom>(out var bloom))
    bloom.intensity.value = 1.5f;
```

Anti-aliasing options on the Unity side include TAA, FXAA, SMAA, and in HDRP, hardware upscaler integration (DLSS/FSR/XeSS).

---

## 3. Scene Graph / World Representation

### 3.1 Unreal: World / Level / Actor / Component

A `UWorld` is the top-level container for a play session, holding one or more `ULevel`s (a persistent level plus any streaming sub-levels). Each `ULevel` contains `AActor` instances, and an `AActor` is a reflected, network-replicable container object that composes behavior via `UActorComponent`s [53][54]. Unreal's component model is tiered rather than flat: **ActorComponent** has no transform and is used for abstract behavior such as inventory management; **SceneComponent** adds a 3D transform and forms an attachment hierarchy — the closest analog to a classic scene-graph node; and **PrimitiveComponent** adds renderable or collidable geometry on top of a SceneComponent [55]. This tiering bakes "does this have a transform" and "is this renderable" into the type hierarchy itself, so engine code can make compile-time guarantees (`AttachToComponent`, for instance, only accepts a `USceneComponent`). Actors follow a well-defined lifecycle — `PostInitProperties → PreInitializeComponents → InitializeComponents → BeginPlay → Tick → EndPlay` [56]:

```cpp
UCLASS()
class AMyActor : public AActor
{
    GENERATED_BODY()
public:
    AMyActor()
    {
        RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
        Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
        Mesh->SetupAttachment(RootComponent);
    }
    virtual void BeginPlay() override { Super::BeginPlay(); }
private:
    UPROPERTY(VisibleAnywhere) UStaticMeshComponent* Mesh;
};
```

### 3.2 Unity: Scene / GameObject / Transform

Unity solves the same problem — hierarchical spatial composition plus pluggable behavior — with a flatter, more uniform model. A **Scene** is a serialized container of a flat list of `GameObject`s, each of which owns exactly one `Transform` (or `RectTransform` for UI), forming the parent-child hierarchy visible in the Hierarchy window [57][58]. Every other behavior — rendering (`MeshRenderer`), physics (`Rigidbody`, `Collider`), scripting (`MonoBehaviour` subclasses) — is attached as a sibling `Component` on the same `GameObject`. Unlike Unreal's three-tier component model, any `Component` subclass can be attached to any `GameObject`, and only `Transform` is mandatory; the "does this need a transform" distinction Unreal enforces at compile time is left to runtime convention in Unity:

```csharp
public class MyBehaviour : MonoBehaviour
{
    void Awake()
    {
        var child = new GameObject("Child");
        child.transform.SetParent(transform);
        gameObject.AddComponent<MeshRenderer>();
    }
}
```

Both models are, at bottom, composition-over-inheritance solutions to the same problem, reflecting a broad industry move away from deep inheritance-based scene graphs (of the kind classic libraries like Java3D or OpenSceneGraph used). Unreal's tiering trades simplicity for compile-time guarantees; Unity's flat, uniform `Component` model trades those guarantees for lower conceptual overhead and easier composability — attach any component to any object, with type-safety checks (if any) deferred to runtime.

### 3.3 World Partition vs. Scene Streaming / Addressables

Open-world games cannot load an entire world into memory at once, so both engines provide systems to stream world content in and out based on player position or visibility.

Unreal's answer, introduced in UE5, is **World Partition**: an automatic, grid-cell-based streaming system where the world is authored as a single persistent level, internally split into a spatial grid of cells that stream in and out based on distance, augmented by **Data Layers** that let designers toggle logical layers — for example "Winter" versus "Summer" content — independent of the streaming grid [16][17]. **HLOD (Hierarchical Level of Detail)** generation automatically merges and simplifies distant cells into cheaper proxy representations [18], and navigation data itself is partitioned and streamed alongside geometry via World Partitioned Navigation Mesh [61]. This gives Unreal projects a first-party, integrated, single-persistent-level open-world workflow — a substantial engineering investment that pays off directly at open-world scale but adds tooling overhead for smaller, level-based games, which is why Unreal still supports the older manual **Level Streaming** model (explicit sub-level volumes and Blueprint-triggered `LoadStreamLevel` calls) for projects that don't need it:

```cpp
// Unreal: Data Layer runtime toggle
UDataLayerSubsystem* DLSub = GetWorld()->GetSubsystem<UDataLayerSubsystem>();
DLSub->SetDataLayerRuntimeState(WinterLayerAsset, EDataLayerRuntimeState::Activated);
```

Unity has no single unified analog to World Partition; open-world streaming there is composed from two orthogonal systems that a studio integrates itself. **Multi-Scene / additive Scene loading** (`SceneManager.LoadSceneAsync(name, LoadSceneMode.Additive)`, typically paired with hand-authored or Terrain-driven streaming-volume logic) controls which *Scenes* are resident, while **Addressables** controls which *assets* are resident, loading and releasing content by string "address" with automatic reference-counted memory management and support for remote content delivery — for example over a CDN, independent of app builds [33]. A studio building a large open world in Unity typically layers a custom streaming-cell manager over these two primitives, functionality Unreal provides natively; in exchange, Unity's Scenes-plus-Addressables combination is more general-purpose, and Addressables also drives DLC and remote-content delivery independent of any streaming use case:

```csharp
// Unity: additive scene streaming + Addressables asset load
SceneManager.LoadSceneAsync("Cell_012", LoadSceneMode.Additive);
Addressables.LoadAssetAsync<GameObject>("props/rock_01").Completed += handle => { /* spawn */ };
```

---

## 4. Physics & Collision Systems

### 4.1 Unreal: Chaos Physics

UE5's physics simulation is **Chaos Physics**, a from-scratch, multithreaded physics and destruction engine that replaced the third-party PhysX/APEX stack used in UE4 [3]. Chaos treats rigid body dynamics, destruction, cloth, vehicles, and networked physics as a unified system rather than a set of bolted-together third-party libraries [3][4][5]. Rigid-body simulation runs through a `UPrimitiveComponent`'s physics state (`BodyInstance`), configured via Physics Assets (skeletal collision hierarchies) and Physical Materials (friction, restitution, density). **Chaos Destruction** builds on the same simulation core to provide Geometry Collections — fracturable meshes authored with Voronoi or clustered fracture patterns that break apart under simulated force in real time, tuned for cinematic-quality destruction at runtime performance budgets [4]. **Chaos Vehicles** is a dedicated vehicle-physics subsystem covering suspension, tire friction models, and engine/transmission simulation [5], and the **Chaos Visual Debugger (CVD)** records and replays simulation state for offline analysis of physics bugs:

```cpp
// Applying an impulse and querying overlap via Chaos-backed physics API
MeshComponent->AddImpulse(FVector(0, 0, 500.f), NAME_None, true);

FHitResult Hit;
FCollisionQueryParams Params(SCENE_QUERY_STAT(MyTrace), false, this);
GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
```

Because Chaos is a first-party system built from scratch, it integrates tightly with World Partition streaming and with Unreal's networking model — networked physics and rewind are first-class Chaos features — but as a from-scratch system it has changed architecturally version to version, and historically had rougher edges around determinism and stability compared to the mature, decades-tuned PhysX it replaced.

### 4.2 Unity: PhysX, Box2D, and Unity Physics (DOTS)

Unity ships two independent, always-available physics backends: **NVIDIA PhysX** for 3D physics, integrated in close partnership with NVIDIA, and **Box2D** for 2D physics [51][52] — both mature, battle-tested third-party engines rather than first-party code. In the GameObject/MonoBehaviour world, physics is configured via `Rigidbody`/`Rigidbody2D` and `Collider`/`Collider2D` components, simulated during `FixedUpdate`, with collision and trigger callbacks (`OnCollisionEnter`, `OnTriggerStay`, and so on) dispatched to `MonoBehaviour`s on the involved GameObjects:

```csharp
public class Projectile : MonoBehaviour
{
    public float force = 500f;
    void Start() => GetComponent<Rigidbody>().AddForce(transform.forward * force);
    void OnCollisionEnter(Collision collision)
    {
        if (collision.gameObject.TryGetComponent<Health>(out var hp))
            hp.TakeDamage(10f);
    }
}
```

Separately, the DOTS ecosystem provides **Unity Physics** (`com.unity.physics`), a deterministic, Burst-compiled, stateless ECS-native physics package designed to interoperate with Havok Physics for Unity as an optional, higher-fidelity drop-in solver operating over the same ECS data — that is, Havok plugs in as an alternative *solver* for Unity Physics's data layout rather than replacing PhysX in the GameObject world. This gives DOTS projects a deterministic, highly parallel simulation path independent of the GameObject pipeline, but accessing it means committing to the ECS programming model rather than staying in MonoBehaviour-land. Studios needing rock-solid, widely licensed simulation, or 2D physics specifically, lean on the PhysX/Box2D stack; studios needing large-scale, first-party destruction gravitate to Chaos on the Unreal side.

### 4.3 Collision Detection, Rigid Bodies, Constraints

Both engines separate *broad-phase* collision detection — cheap bounding-volume culling of candidate pairs — from *narrow-phase* detection — exact shape-vs-shape intersection — and both support discrete and continuous collision detection (CCD, to prevent fast-moving objects tunneling through thin geometry). Both also provide constraint and joint solvers for ragdolls, vehicles, and mechanical assemblies: Unreal exposes **Physics Constraints** (`UPhysicsConstraintComponent`, or via a Physics Asset's constraint hierarchy for ragdolls) with linear and angular limits and drives, while Unity exposes a family of `Joint` components (`HingeJoint`, `ConfigurableJoint`, `FixedJoint`, `SpringJoint`) with equivalent limit and drive semantics — the underlying constraint-solving concept (limits, springs, drives between two bodies) is shared, with each engine exposing it through its own component vocabulary.

---

## 5. Animation Systems

### 5.1 Skeletal Animation Fundamentals

Skeletal (skinned) animation deforms a mesh via a hierarchy of bones/joints, with each animation clip storing per-bone transform curves — or per-frame samples — played back and blended at runtime. A "skeleton" asset defines the bone hierarchy shared across every animation and mesh that uses it, which is what allows one walk-cycle clip to be reused across multiple characters that share a rig, and what makes retargeting between skeletons possible.

### 5.2 Animation Blueprints vs. Mecanim/Animator

Runtime character animation is almost universally driven by a parameter-fed state machine layered on top of a pose-blending graph: gameplay code writes a handful of parameters (speed, is-in-air, aim angle) each frame, and the animation system uses those parameters both to pick which state (idle, walk, run, jump) is active and to blend between the poses that state produces.

In Unreal, skeletal meshes reference a shared `USkeleton` asset, and per-character runtime animation logic lives in an **Animation Blueprint** (`UAnimInstance` subclass). This contains an **AnimGraph** — a full node-based *dataflow* graph blending poses via Blend Spaces, layered blends, and montages — driven by an **EventGraph** that in turn drives a **State Machine** [41]. State Machines break character animation into discrete states connected by transition rules evaluated every frame, and can be nested and composed via **Animation Blueprint Linking**, letting large characters split state logic across multiple linked instances. **Montages** layer one-off, code- or Blueprint-triggered animations — attacks, reloads — on top of the locomotion state machine, with per-section branching and **AnimNotify** events for synchronizing gameplay (footstep sounds, hit windows) to specific animation frames:

```cpp
// UAnimInstance-derived class updating properties read by the AnimGraph
void UMyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    if (APawn* Pawn = TryGetPawnOwner())
    {
        Speed = Pawn->GetVelocity().Size();
        bIsInAir = Pawn->GetMovementComponent()->IsFalling();
    }
}
```

Unity's animation system, **Mecanim**, follows the same parameter-driven-state-machine pattern through an `Animator` component referencing an **Animator Controller** asset — a flowchart-like graph of **Animator States** organized into **Layers**, connected by **Transitions** whose conditions are evaluated against script-driven **Animation Parameters** (float, int, bool, trigger) [42][43]. **Avatar Masking** and multiple layers allow partial-body blending — an upper-body aim layer over a locomotion base layer, for instance — the direct analog of Unreal's layered blend-per-bone nodes:

```csharp
public class LocomotionController : MonoBehaviour
{
    Animator animator;
    void Awake() => animator = GetComponent<Animator>();
    void Update()
    {
        animator.SetFloat("Speed", GetComponent<Rigidbody>().velocity.magnitude);
        animator.SetBool("IsInAir", !IsGrounded());
    }
}
```

The two systems are close structural cousins, but Unreal's AnimGraph is a full dataflow graph — blend nodes, IK, physics-driven animation, all composable per-frame — where Mecanim's graph is primarily a *state* graph; complex custom pose blending in Unity more often requires dropping to the lower-level Playables API or the Animation Rigging package rather than expressing it purely in graph form. That gives Unreal's animators and technical animators more in-graph power at the cost of a steeper learning curve, against Mecanim's faster onboarding for standard locomotion-style state machines.

### 5.3 Inverse Kinematics

Inverse kinematics solves for the joint angles of a bone chain given a desired end-effector position — the standard technique for planting a foot on uneven terrain, or aiming a hand at a target regardless of the base animation.

Unreal expresses IK as AnimGraph nodes: **Two Bone IK** for simple limb IK, **FABRIK** for chain IK, and **Control Rig** — a full node-based rigging and IK system usable both in the editor and at runtime, which also drives procedural animation more generally. Unity splits the same capability across two tools: built-in **Animator IK** callbacks (`OnAnimatorIK`, with `SetIKPosition`/`SetIKRotation`/`SetIKPositionWeight` for hands, feet, and look-at targets) for humanoid rigs, and the separate **Animation Rigging** package (constraint-based: Two Bone IK Constraint, Multi-Parent Constraint, Chain IK Constraint) for non-humanoid or more complex rigs, built on the Playables graph:

```csharp
void OnAnimatorIK(int layerIndex)
{
    animator.SetIKPositionWeight(AvatarIKGoal.RightHand, 1f);
    animator.SetIKPosition(AvatarIKGoal.RightHand, targetHandle.position);
}
```

Unreal's Control Rig unifies IK, procedural animation, and rigging into one node-graph tool usable by riggers and runtime code alike; Unity's split between a simple humanoid-only built-in path and a more general but separately-adopted package means non-humanoid IK requires bringing in an additional authoring workflow rather than using the same tool throughout.

### 5.4 Blend Spaces

Multi-clip interpolation over a parameter grid is the standard technique for locomotion blending — walk, run, and strafe clips sampled and blended smoothly as speed and direction change. Unreal calls this a **Blend Space**: a 1D or 2D parameter grid (Speed × Direction, for example) with animation clips sampled at grid points, interpolated by the AnimGraph based on runtime parameter values. Blend Spaces are standalone, reusable assets referenceable from multiple Animation Blueprints. Unity's equivalent is the **Blend Tree** node type inside an Animator Controller state, supporting 1D, 2D Simple Directional, 2D Freeform Directional, and 2D Freeform Cartesian blending modes, parameterized the same way via script-driven float parameters — functionally near-identical to a Blend Space, but defined inline within a specific Animator Controller state rather than as a standalone asset, which makes cross-controller reuse more manual (typically copy-paste or a shared sub-state-machine) than Unreal's reusable-asset approach.

---

## 6. Audio Systems

### 6.1 Unreal: MetaSounds & the Audio Engine

Procedural, sample-accurate audio synthesis and processing is increasingly how AAA engines handle interactive sound — rather than triggering pre-mixed clips, a DSP graph generates and shapes audio at the sample or block level in response to gameplay parameters. UE5's implementation of this idea is **MetaSounds**, a node-based DSP graph system that generates and processes audio procedurally, and which has replaced the older Sound Cue system as the primary sound-source authoring tool [6][7]. Sound designers build graphs from DSP primitives — oscillators, filters, envelopes — and higher-level nodes without programming; MetaSound graphs can be composed inside other MetaSound graphs (graph composition), and custom nodes can be authored in C++ as engineering-owned building blocks [6]. The wider Audio Engine surrounding MetaSounds provides mixing (Submixes, Sound Classes/Mixes), distance- and occlusion-based attenuation, and Audio Modulation for real-time parameter control:

```cpp
// Triggering a MetaSound-based sound source from gameplay code
UGameplayStatics::SpawnSoundAtLocation(this, FootstepMetaSound, GetActorLocation());
```

### 6.2 Unity: Audio, FMOD, and Wwise Integration Patterns

Unity's built-in audio stack takes a lighter-weight approach centered on `AudioSource`/`AudioClip`/`AudioMixer`, with Mixer Groups, Snapshots for state-based mix transitions, and effects like Reverb and Filters — sufficient for straightforward playback and mixing, but comparatively basic next to a procedural DSP-graph tool like MetaSounds:

```csharp
// Unity native audio
AudioSource.PlayClipAtPoint(footstepClip, transform.position);
```

For advanced interactive audio — procedural mixing, complex parameter-driven sound design, dynamic music systems — the standard pattern in Unity projects is to integrate dedicated middleware, most commonly **Wwise** (Audiokinetic) or **FMOD**, via their official Unity integration packages. These replace or augment `AudioSource` with their own event-driven APIs while Unity handles spatialization plumbing and object lifecycle:

```csharp
// FMOD integration pattern (via FMOD for Unity package)
FMODUnity.RuntimeManager.PlayOneShot("event:/SFX/Footstep", transform.position);
```

This is a well-trodden, well-supported path for mid-to-large productions, but it adds a third-party dependency, license cost, and a second authoring tool — the standalone Wwise or FMOD application — outside Unity itself. It's worth noting this mirrors Unreal's own audio history: before MetaSounds matured into a first-party replacement, Unreal projects leaned on the same Wwise/FMOD integrations that remain the default answer for sophisticated audio on Unity today.

### 6.3 Spatial Audio

Spatial (3D) audio attenuates and filters sound based on listener-relative position, increasingly with HRTF-based binauralization for headphone playback, plus occlusion and obstruction modeling against level geometry. Unreal's Attenuation Settings assets control distance-based volume and low-pass falloff, spatialization plugins (including built-in HRTF-based binaural spatialization), and Occlusion traces against collision geometry, with Ambisonics and Audio Volumes providing reverb-zone and submix-routing based on player location. Unity's `AudioSource.spatialBlend` controls the 2D/3D mix, with pluggable **Spatializer SDKs** (via `AudioSettings.SetSpatializerPluginName`, historically including Oculus, Microsoft, and Resonance Audio spatializers) for HRTF binaural rendering, and Reverb Zones for geometry-based ambient reverb transitions. Both provide comparable baseline capability here; the meaningful difference again traces back to whether a studio stays in-engine (sufficient for most Unreal projects thanks to MetaSounds) or brings in Wwise/FMOD (common on both engines for AAA-scale interactive music and mixing), in which case spatial audio quality is typically inherited from the middleware rather than the host engine.

---

## 7. Input Systems

### 7.1 Unreal Enhanced Input

Modern input systems abstract raw device signals behind named, context-switchable gameplay actions, so that "Jump" means the same thing to gameplay code whether it came from a keyboard key or a gamepad button, and so that the same physical input can mean different things depending on context (on-foot versus driving, for example). Unreal's implementation of this pattern is **Enhanced Input** — the default input system since UE5, superseding the legacy Action/Axis Mapping system — built around four concepts [8]: **Input Actions**, abstract, device-agnostic gameplay-facing input events; **Input Mapping Contexts (IMCs)**, a swappable, priority-ordered set of raw-input-to-Action bindings (an "OnFoot" context versus a "Driving" context, for instance, or per-platform bindings); **Input Modifiers**, which transform raw input — dead zones, negation, scaling — before it reaches gameplay; and **Input Triggers**, which define *when* an Action fires (Pressed, Held, Tap, Chorded Action):

```cpp
// Binding an Input Action in a Pawn/Controller, with contextual mapping
void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);
        EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &APlayerCharacter::Jump);
    }
    if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetController<APlayerController>()->GetLocalPlayer()))
    {
        Subsystem->AddMappingContext(OnFootMappingContext, /*Priority=*/0);
    }
}
```

### 7.2 Unity Input System Package

Unity's **Input System package** (`com.unity.inputsystem`) implements the same abstraction pattern, and is a near-exact structural analog of Enhanced Input — a case of convergent design, since both superseded older "hardcoded axis/button name" input systems in roughly the same generation [15]. It centers on **Input Actions**, abstract gameplay events grouped into **Action Maps** (a "Gameplay" map versus a "UI" map, for example), each bindable to multiple physical **Controls** across device types — keyboard, gamepad, touch, XR — with **Processors** (dead zone, scale, invert; the analog of Enhanced Input's Modifiers) and **Interactions** (Press, Hold, Tap, MultiTap; the analog of Enhanced Input's Triggers). **Player Input** components, or generated C# wrapper classes from an Input Actions asset, provide code-facing bindings, and runtime rebinding is supported via `InputActionRebindingExtensions`:

```csharp
// Generated Input Actions wrapper, bound to callbacks
public class PlayerController : MonoBehaviour
{
    PlayerInputActions actions;
    void OnEnable()
    {
        actions = new PlayerInputActions();
        actions.Gameplay.Move.performed += ctx => moveInput = ctx.ReadValue<Vector2>();
        actions.Gameplay.Jump.started += ctx => Jump();
        actions.Enable();
    }
}
```

Enhanced Input's Mapping Contexts give slightly more explicit runtime layering and priority semantics for context switching — stacking a "Paused" context above "Gameplay," for instance — where Unity's Action Maps achieve the same effect by enabling or disabling whole maps. Unity's system has a particular advantage for cross-device and XR titles, since its unified Control/Device abstraction covers an unusually broad device matrix, including XR controllers, out of the box.

---

## 8. Scripting & Gameplay Frameworks

### 8.1 Blueprints vs. C++ and the Gameplay Ability System

Unreal offers two first-class, interoperable programming surfaces for gameplay logic — native **C++** and the visual scripting system **Blueprints** — both operating over the same reflected `UObject`/`AActor` type system, so a Blueprint can subclass a C++ class (and vice versa via Blueprint-callable functions) with no serialization boundary between them. This is a single runtime model exposed through two authoring surfaces, not a data-layout tradeoff: C++ gameplay classes expose `UFUNCTION(BlueprintCallable)`/`UPROPERTY(BlueprintReadWrite)`-tagged members to Blueprint graphs, letting engineers build performance-critical or complex systems in C++ while designers iterate on tuning, VFX/animation hookup, and high-level flow in Blueprint — a deliberate two-tier workflow where the dial between the two is productivity versus performance, not architecture versus architecture.

For complex stat-driven gameplay — RPGs, MOBAs, shooters with abilities, cooldowns, and buffs — Unreal ships the **Gameplay Ability System (GAS)** as a purpose-built, network-aware layer on top of this model. GAS is built around `UAbilitySystemComponent` (the per-actor hub), `UGameplayAbility` (individual abilities with activation, cost, and cooldown logic), `FGameplayAttribute`/`UAttributeSet` (stat storage with clamping and calculation hooks), and `UGameplayEffect` (data-driven, replicated modifiers — damage, buffs, damage-over-time — applied via `GameplayEffectComponents`) [9][10]. GAS is explicitly designed for networked games: abilities, effects, and attribute changes replicate through the ability system component rather than requiring bespoke per-feature replication code [9]:

```cpp
UCLASS()
class UGA_Fireball : public UGameplayAbility
{
    GENERATED_BODY()
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData) override
    {
        // Apply cost/cooldown GameplayEffects, spawn projectile, commit ability
        CommitAbility(Handle, ActorInfo, ActivationInfo);
        K2_EndAbility();
    }
};
```

### 8.2 MonoBehaviour vs. DOTS/Entities

Unity's default scripting surface is C# `MonoBehaviour` subclasses attached to GameObjects — standard C# OOP (inheritance, interfaces, events) with Unity-specific lifecycle callbacks (`Awake`, `Start`, `Update`, `OnDestroy`, and so on) invoked reflectively through the native engine's managed-callback registry. Unlike Unreal's C++/Blueprint duality, where both surfaces share one runtime model, Unity's alternative high-performance surface — the **Entities** package's ECS model (see [Section 1.3](#13-ecs-vs-oopgameobject-component-models)) — is a genuinely different runtime execution model, not just a different API: Burst-compiled jobs over chunked memory rather than virtual-dispatched per-object calls. Moving performance-critical gameplay from MonoBehaviour to Entities is therefore a rewrite, not a refactor.

DOTS/Entities gameplay is written as `IComponentData` plus `ISystem`/`SystemBase`, explicitly avoiding per-instance managed allocations and virtual dispatch, and interoperates with GameObject-world code via **GameObject conversion / baking** — the `Baker`/`IBaker` API converts authoring-time GameObjects into runtime entities — so a project can mix both models, using GameObjects for editor-authored, low-count content and entities for high-count simulated content:

```csharp
// Baker: converts a GameObject prefab's authoring data into ECS components
public class BulletAuthoring : MonoBehaviour { public float speed; }
public class BulletBaker : Baker<BulletAuthoring>
{
    public override void Bake(BulletAuthoring authoring)
    {
        var entity = GetEntity(TransformUsageFlags.Dynamic);
        AddComponent(entity, new BulletSpeed { Value = authoring.speed });
    }
}
```

There is also no first-party analog to GAS on the Unity side: studios building networked RPG- or MOBA-style stat systems typically construct a bespoke attribute/ability layer atop MonoBehaviour (or ECS `IComponentData`), or adopt third-party packages modeled on GAS design patterns, rather than adopting an engine-provided framework — a gap that requires more from-scratch engineering in Unity than the equivalent Unreal project. Unity's Entities package, in exchange, is more purely data-oriented across the board than Mass Entity is positioned within Unreal — Mass is a specialized subsystem for specific high-agent-count use cases, where Entities is a general-purpose alternative to the default object model — giving Unity teams a cleaner high-performance path once they commit to it.

---

## 9. UI Systems

### 9.1 Unreal: UMG & Slate

Runtime and editor UI in a game engine typically split into a low-level widget/rendering framework and a designer-facing authoring layer built on top of it. In Unreal, **Slate** is the low-level, declarative C++ UI framework — used to build the entire Unreal Editor itself as well as performance-critical runtime UI — written with a fluent widget-construction syntax. **UMG (Unreal Motion Graphics)** is the designer-facing layer on top: a visual, drag-and-drop **Widget Blueprint** editor for constructing runtime game UI (HUDs, menus, inventories) from Slate-backed widgets (`UUserWidget`), with full Blueprint scripting for logic and data binding. Because UMG *is* Blueprint-authored Slate, there is no fragmentation between "editor UI system" and "game UI system" — one coherent stack serves both, and every runtime UI project uses it regardless of complexity.

Designers compose widget hierarchies in the UMG Designer — Canvas Panel, Vertical/Horizontal Box, Border, Text, Button, and so on, all thin UMG wrappers around underlying `SWidget` Slate primitives — binding properties to C++/Blueprint functions for live data such as health bars or ammo counters, either via "Binding" functions evaluated each frame or push-model setter calls for performance-sensitive UI:

```cpp
// Slate: constructing a widget tree directly in C++ (used for editor/tool UI)
SNew(SVerticalBox)
+ SVerticalBox::Slot().AutoHeight()
[
    SNew(STextBlock).Text(FText::FromString(TEXT("Health")))
]
+ SVerticalBox::Slot()
[
    SNew(SProgressBar).Percent(this, &SHealthWidget::GetHealthPercent)
];
```

### 9.2 Unity: UGUI & UI Toolkit

Unity currently maintains two parallel, non-unified UI systems rather than one coherent stack, reflecting a UI-architecture transition that is still in progress industry-wide. **UGUI** (`com.unity.ugui`, "Unity UI", released 2014) is GameObject-based: every UI element is a `GameObject` with a `RectTransform` plus rendering components (`Image`, `Text`/`TextMeshPro`, `Button`), composed under a `Canvas` root that batches draw calls and laid out via anchor/pivot rect transforms and Layout Group components [25]. Because UI elements are real GameObjects, Animator and Timeline tooling work natively on them, which makes UGUI well suited to animated in-world or HUD elements — but the GameObject-per-element overhead hurts mobile batching and performance at scale:

```csharp
// UGUI: GameObject-based UI manipulated via component references
public class HealthBarUGUI : MonoBehaviour
{
    public Slider slider;
    public void SetHealth(float pct) => slider.value = pct;
}
```

**UI Toolkit** (released 2019, and the system used for the Unity Editor's own UI since that release) is fundamentally different: no GameObjects at all — UI is defined via **UXML** (structure, analogous to HTML) and **USS** (styling, a CSS subset), rendered by a retained-mode, batched renderer through a `UIDocument` component referencing a `PanelSettings` asset [25][26]. This gives UI Toolkit single-draw-call-per-panel batched rendering and better performance at scale, plus CSS/web-like authoring, at the cost of younger tooling and weaker Animator/Timeline integration:

```csharp
// UI Toolkit: UXML/USS-defined UI, queried and manipulated via UIDocument
public class HealthBarUIT : MonoBehaviour
{
    void OnEnable()
    {
        var root = GetComponent<UIDocument>().rootVisualElement;
        var bar = root.Q<ProgressBar>("health-bar");
        bar.value = 75f;
    }
}
```

Unity's own guidance is that both systems can coexist in a single project — UGUI for animated in-world/HUD elements, UI Toolkit for data-heavy menus and tooling [25][26] — a pragmatic accommodation of the fact that the migration from one system to the other is ongoing rather than complete, a question Unreal resolved years ago by having one first-party stack serve both editor and game UI.

---

## 10. Networking & Multiplayer

### 10.1 Replication Models & RPCs

Multiplayer engines need to synchronize authoritative game state — usually server-owned — to clients efficiently, plus provide directed remote-procedure-call channels for one-off events (spawn effects, play sounds) that don't fit a continuous state-replication model. Both engines default to a **client-server, server-authoritative** topology, combining automatic **state replication** (property values synced without per-property networking code) with explicit **RPCs** (function calls sent across the network with a defined direction and reliability).

In Unreal, actors opt into replication via `bReplicates = true`, and individual `UPROPERTY(Replicated)` members sync automatically — optionally with `DOREPLIFETIME_CONDITION` for conditional replication, such as replicating only to the owning client — typically driven by delta-compressed, relevancy- and priority-weighted updates computed per connection. RPCs are declared with `UFUNCTION(Server|Client|NetMulticast, Reliable|Unreliable)`: a `Server` RPC runs on the server when called from an owning client, a `Client` RPC runs only on that actor's owning client's machine, and `NetMulticast` runs on the server and every client:

```cpp
UPROPERTY(ReplicatedUsing = OnRep_Health)
float Health;

void AMyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMyCharacter, Health);
}

UFUNCTION(Server, Reliable)
void ServerFire();
void AMyCharacter::ServerFire_Implementation() { /* authoritative fire logic */ }
```

Unity's **Netcode for GameObjects (NGO)** implements the identical pattern: `NetworkObject`/`NetworkBehaviour` is the replication unit, `NetworkVariable<T>` fields auto-sync from server (or owner, if configured) to observing clients, and `[ServerRpc]`/`[ClientRpc]`-attributed methods provide the RPC channel — directly analogous to Unreal's `Server`/`Client`/`NetMulticast` UFUNCTIONs [13][14]:

```csharp
public class PlayerHealth : NetworkBehaviour
{
    public NetworkVariable<float> Health = new NetworkVariable<float>(100f);

    [ServerRpc]
    void FireServerRpc()
    {
        // authoritative hit-detection and Health.Value mutation on server
    }
}
```

### 10.2 Client Prediction & Reconciliation

To hide network latency, clients simulate their own input locally ("prediction") ahead of server confirmation, then reconcile — replay or correct — when an authoritative server state arrives that disagrees with the predicted local state. This is one of the harder pieces of multiplayer engineering, and the two engines diverge sharply in how much of it is provided out of the box.

Unreal's `UCharacterMovementComponent` implements client-side prediction with server reconciliation for movement as a built-in feature: the client moves immediately on input, tags each move, the server re-simulates and returns a correction (`ClientAdjustPosition`) if the client's predicted state diverges beyond tolerance, and the client re-plays subsequent buffered moves from the corrected state. For ability and gameplay-effect prediction beyond movement, GAS layers on **Gameplay Cues** and prediction keys to speculatively apply cosmetic or gameplay effects client-side pending server confirmation [9]. This built-in, heavily battle-tested character-movement prediction is a major reason action- and shooter-genre multiplayer projects gravitate to Unreal — it was built for, and by, a studio that ships AAA competitive multiplayer shooters.

Unity's NGO does not ship a general-purpose predicted-movement solution comparable to `CharacterMovementComponent` for arbitrary gameplay out of the box; it provides the primitives — owner-authoritative `NetworkTransform` with client-side prediction and reconciliation support added in NGO 2.x, plus `NetworkRigidbody` — and studios commonly layer custom prediction on top, or adopt third-party solutions built for this purpose. This reflects NGO's more general-purpose, lower-level design intent (supporting many genres and topologies, including relay and distributed-authority models), which gives more flexibility to implement whatever prediction model fits a given game, but pushes more of the hard networking engineering onto the studio than Unreal requires.

### 10.3 Unreal's Replication Graph

The default Unreal replication path evaluates relevancy and priority per-actor-per-connection every server tick, which becomes a CPU bottleneck once player and actor counts climb. The **Replication Graph** plugin restructures this scheduling problem: actors are placed into **Replication Graph Nodes** — spatial grid nodes, always-relevant nodes, class-based nodes — once, and per-connection replication becomes a matter of querying precomputed node lists rather than re-evaluating every actor against every connection each tick. This is the architecture Epic built to support Fortnite Battle Royale's roughly 100 players and roughly 50,000 replicated actors per match [22][23][24]:

```cpp
// Custom ReplicationGraph: routing an actor class into a spatialization node
void UMyReplicationGraph::InitGlobalActorClassSettings()
{
    Super::InitGlobalActorClassSettings();
    FClassReplicationInfo Info;
    Info.SetCullDistanceSquared(15000.f * 15000.f);
    GlobalActorReplicationInfoMap.SetClassInfo(APickupActor::StaticClass(), Info);
}
```

The Replication Graph is explicitly opt-in over the simpler default replication path, recommended once a project's actor and connection count outgrows the default per-actor relevancy loop — it is powerful, but adds real complexity in the form of custom node classes and per-class configuration.

### 10.4 Unity Netcode for GameObjects

Netcode for GameObjects is Unity's official first-party high-level networking library for the GameObject/MonoBehaviour workflow, sitting atop a pluggable **Unity Transport** layer [13][14]. It supports both traditional server-authoritative topologies and, in newer versions, **Distributed Authority** — ownership and authority can move between clients per-object, useful for physics-heavy or peer-hosted scenarios — as an alternative to strict server authority, a networking topology option Unreal doesn't provide as a first-party alternative to server authority. Object spawning and despawning, `NetworkVariable` delta-replication, and scene management (`NetworkSceneManager`) round out the package. NGO's architecture is comparatively simpler to get started with than the Replication Graph, but Unity's high-level networking ecosystem is less battle-tested at Fortnite-scale player and actor counts, and has changed its recommended package more than once across the engine's history — evolving from UNet through MLAPI to NGO — where Unreal's replication model has been comparatively stable since UE3/UE4.

---

## 11. Asset Pipeline & Content Management

### 11.1 Import Pipelines

Source content authored in DCC tools — meshes, textures, audio — must be imported, validated, and converted into engine-native runtime formats, with import settings tracked per-asset so re-imports are reproducible and the source file remains the editable "truth" rather than the engine-native copy.

Unreal handles import per-asset-type through dedicated **Factories** (`UFactory` subclasses) and per-type Import UI — FBX Import Options for meshes and animations, texture import settings for compression and mip generation — with source file paths tracked for **Reimport**. The newer **Interchange** framework (UE5) is a pipeline-based, extensible replacement for the legacy per-type import path, unifying and scripting mesh, material, texture, and animation import through configurable Interchange Pipelines.

Unity processes every asset dropped into a project's `Assets/` folder through an **AssetImporter** (`ModelImporter`, `TextureImporter`, `AudioImporter`), which serializes its settings into a sibling `.meta` file carrying the asset's stable GUID. `AssetPostprocessor` callbacks (`OnPreprocessModel`, `OnPostprocessTexture`, and so on) let studios script custom import behavior or validation per asset type:

```csharp
// Unity: scripted import-time processing
public class TexturePostprocessor : AssetPostprocessor
{
    void OnPreprocessTexture()
    {
        var importer = (TextureImporter)assetImporter;
        if (assetPath.Contains("_Normal")) importer.textureType = TextureImporterType.NormalMap;
    }
}
```

### 11.2 Unreal Asset Registry

Large projects need to query metadata about assets — name, class, searchable tags — without paying the cost of loading every asset just to inspect it. Unreal's answer is the **Asset Registry**, an editor (and cook-time) subsystem that asynchronously gathers this metadata for every asset in a project without loading the asset itself, enabling instant content-browser search, filtering, and dependency queries even across huge projects [31]. Any `UPROPERTY` marked `AssetRegistrySearchable` is captured into each asset's `FAssetData.TagsAndValues` map at save time, so custom queryable metadata is opt-in and declarative, and delegate callbacks (`OnAssetAdded`, `OnAssetRemoved`, `OnAssetRenamed`) let editor tooling react to registry changes live [31]. Because the registry deliberately avoids loading assets, it feeds directly into cook-time dependency resolution (see [Section 11.4](#114-cooking--building)) without adding load cost to that process [31][48]:

```cpp
// Querying the Asset Registry for all Blueprint assets under a path
IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
TArray<FAssetData> Assets;
AssetRegistry.GetAssetsByPath(FName("/Game/Characters"), Assets, /*bRecursive=*/true);
```

### 11.3 Unity AssetDatabase & Addressables

Unity splits the same underlying need — asset metadata lookup plus runtime content loading — across two separate systems rather than one. **AssetDatabase** is the editor-only API for locating, loading, creating, and modifying project assets by path or GUID (`AssetDatabase.LoadAssetAtPath`, `FindAssets`), the closest analog to Unreal's Asset Registry, though more directly load-capable since it's editor-only and Unity's asset objects are cheap to reference by GUID [32]. **Addressables** (`com.unity.addressables`) is the runtime-facing system: assets are marked "addressable," given a string or label **address**, and loaded or released by address with automatic reference counting and memory management — critically, including support for building content into separately deployable, potentially remote-hosted asset bundles independent of the main application binary, enabling patch or DLC delivery without an app-store resubmission [33]:

```csharp
// AssetDatabase (editor-only)
var guids = AssetDatabase.FindAssets("t:Prefab", new[] { "Assets/Characters" });

// Addressables (runtime, editor and player)
Addressables.LoadAssetAsync<GameObject>("enemy_goblin").Completed += OnLoaded;
```

Because Unity's "asset registry" and "runtime content delivery" concerns are two different systems that a developer has to learn separately, where Unreal's Asset Registry directly informs its own cooker, the practical effect is that Unity developers reach for AssetDatabase during editor tooling and for Addressables during runtime loading as two distinct mental models, while Unreal developers use one system, the Asset Registry, that spans both concerns.

### 11.4 Cooking & Building

Both engines transform editor-format content into optimized, platform-specific runtime data as part of producing a shippable build. In Unreal, **Cooking** converts each asset from its editable in-editor representation into a platform-specific runtime format — handling endianness, texture compression, and shader permutation compilation — driven by the Asset Registry's dependency graph to determine what content is actually reachable and needed [48][49]. **Packaging** wraps the build-cook-stage sequence into a distributable executable or platform package: Build compiles code and binaries for the target platform, Cook converts content, and Stage assembles the final deployable directory tree [48][50].

Unity's **Build Pipeline** (`BuildPipeline.BuildPlayer`, or the Build Settings window) compiles scripts, strips unused code via IL2CPP and Managed Stripping settings, and packages Scenes plus any Addressables-marked content into a platform build. Addressables content is built separately, via the Addressables Groups window and `AddressablesPlayerBuildProcessor`, into platform-specific asset bundles that can either ship inside the player build or be uploaded to a remote content host.

---

## 12. Memory Management & Performance

### 12.1 Garbage Collection vs. Manual/Reference-Counted Memory

C++ — Unreal's native language — has no automatic memory management; C# — Unity's scripting language, running on the CLR/Mono/IL2CPP runtime — is garbage-collected. An engine built primarily in C++ needs its own object-lifetime story for engine-level objects, so that gameplay programmers aren't required to hand-manage every pointer, without paying the runtime cost of tracing the engine's entire memory space the way a general-purpose GC would.

Unreal's solution is a hybrid, scoped model. `UObject`-derived types — Actors, Components, most engine and gameplay classes — are managed by Unreal's own **tracing garbage collector**: `UPROPERTY()`-marked pointers are tracked as GC roots or references, and periodically the GC traces reachability from root sets and destroys unreferenced `UObject`s, conceptually similar to a mark-and-sweep collector but scoped only to reflected `UObject`s, not all C++ memory. Non-`UObject` C++ memory is manually managed — raw `new`/`delete`, custom allocators — or reference-counted via `TSharedPtr`/`TWeakPtr`/`TUniquePtr`, Unreal's `boost`-inspired smart pointer library used pervasively for non-UObject engine types like Slate widgets. This gives Unreal automatic GC for the reflected gameplay object graph, and manual/RAII/smart-pointer discipline for everything else — with the corresponding responsibility on engineers to null out or weak-reference raw `UObject*` pointers correctly to avoid dangling references in the window before GC runs, and the corresponding benefit that only a fraction of engine memory is ever traced:

```cpp
UPROPERTY() // GC root-traced; safe against dangling pointers
AActor* TargetActor;

TSharedPtr<FMyNonUObjectData> Data = MakeShared<FMyNonUObjectData>(); // manual refcounting
```

Unity takes the opposite approach: every C# managed object, including every `MonoBehaviour` and any class instance, is collected by the runtime's garbage collector, with no scoping decision to make. Unity historically used the **Boehm-Demers-Weiser** conservative GC; modern Unity (2021.2+) defaults to the **Incremental Garbage Collector**, which splits collection work across multiple frames to avoid a single long stop-the-world pause and the resulting frame-time spike, and exposes `GarbageCollector` APIs to tune or manually invoke collection [36][37][38]:

```csharp
// Unity: configuring incremental GC and forcing a collection pass
GarbageCollector.GCMode = GarbageCollector.Mode.Enabled;
System.GC.Collect(); // explicit, generally avoided in hot paths
```

This fully managed model is far simpler to reason about for typical gameplay code than Unreal's two-regime split, but it makes accidental GC pressure — from boxing, LINQ, or string concatenation in hot paths — a much more common real-world performance problem than in Unreal's scoped-GC model, which is precisely the pressure that motivated Burst, the C# Job System, and the Entities package's unmanaged-by-default component data.

### 12.2 Object Pooling

Repeatedly allocating and destroying short-lived objects — projectiles, particles, enemies — is expensive under both a tracing GC (allocation and collection pressure) and manual allocation (fragmentation, allocator overhead). Pooling addresses this by pre-allocating a reusable set of instances and "spawning" by activating and resetting a pooled instance instead of constructing a new one.

Unity ships a first-party generic pooling utility, `UnityEngine.Pool.ObjectPool<T>` (and `GameObjectPool` helpers), with a standard `Get`/`Release` API, configurable max size, and pre-warming — reflecting how central pooling is to managing GC pressure in a fully garbage-collected runtime, where it's treated as a standard, expected pattern with engine-level support:

```csharp
var pool = new ObjectPool<Bullet>(
    createFunc: () => Instantiate(bulletPrefab).GetComponent<Bullet>(),
    actionOnGet: b => b.gameObject.SetActive(true),
    actionOnRelease: b => b.gameObject.SetActive(false),
    maxSize: 100);
Bullet b = pool.Get();
```

Unreal has no single universal built-in Actor-level pooling framework historically, though UE5 ships a first-party **Actor Pool** utility used internally — for example, for World Partition cell actor reuse — and gameplay code commonly implements manual pools for projectiles and effects (`Deactivate`/`Reset` plus `SetActorHiddenInGame`/`SetActorTickEnabled` instead of `Destroy`/`SpawnActor`); Niagara, the VFX system, pools particle system instances internally. Because Unreal's GC is scoped to `UObject`s, and its allocator is a purpose-built engine allocator rather than a general-purpose OS allocator, raw spawn and destroy are noticeably cheaper than Unity's `Instantiate`/`Destroy`, so pooling in Unreal is more often a targeted optimization for specific very-high-frequency systems — bullets, hit effects — rather than a default pattern applied everywhere the way Unity's first-party `ObjectPool<T>` suggests it should be.

### 12.3 Profiling Tools

Diagnosing performance problems in a real-time engine requires visibility into CPU timing, GPU cost, memory allocation, and — in multiplayer games — network traffic, typically captured either as a live in-editor view or as a recorded trace analyzed offline.

Unreal's primary standalone profiler is **Unreal Insights**, a trace-based system: the engine emits structured trace events via macros like `TRACE_CPUPROFILER_EVENT_SCOPE`, which Insights records, analyzes, and visualizes either offline or over a remote connection, covering CPU timing, the Task Graph, and networking through a dedicated **Networking Insights** telemetry view for network traffic analysis [34]. **Memory Insights** (UE5) extends this with Low Level Memory (LLM) tags and callstack-attributed allocation tracking, letting engineers break memory usage down by engine subsystem [35], and the in-editor **Stat commands** (`stat unit`, `stat game`, `stat gpu`) and GPU Visualizer provide lighter-weight real-time views. The trace-based model excels at capturing long play sessions with low overhead and after-the-fact deep analysis, including on production or remote builds, and folds the two hardest classes of engine bugs — network desyncs and memory bloat — into the same Insights application via its Networking and Memory Insights modules [34][35].

The **Unity Profiler** is an in-editor, and device-attachable, real-time profiler with per-frame module breakdowns — CPU, Rendering, Memory, Audio, Physics, GC Alloc — and a **Profile Analyzer** package for comparing captures across runs or builds. The separate **Memory Profiler** package gives a snapshot-based, object-graph-level view of managed and native heap allocations, useful for tracking down leaks or retained references, and **Frame Debugger** steps through individual draw calls within a frame for rendering diagnostics. This live, per-frame module graph is more immediately accessible for real-time, in-editor iteration than Insights' trace-and-analyze workflow, and its device-attach workflow suits quick mobile-performance iteration well — though deep memory forensics requires the separate Memory Profiler package rather than being unified into the main Profiler window the way Unreal folds Memory Insights into the same Insights application.

---

## 13. AI Systems

### 13.1 Behavior Trees

A Behavior Tree (BT) is a hierarchical, node-based decision structure — Selector/Sequence composite nodes, Decorator condition nodes, Task leaf nodes — evaluated top-down each tick to select and execute the current best action for an AI agent. It is the dominant architecture for game AI decision-making today, having largely superseded Finite State Machines for anything beyond trivial agent logic, because it composes and scales better: new behavior is added by inserting nodes rather than by adding transitions to every existing state.

Unreal's AI stack pairs a visual **Behavior Tree** editor with a **Blackboard** asset — a typed key-value data store shared between the tree and its supporting systems, holding things like a target actor, a last-known location, or a patrol point — which Decorators query and Tasks write to. `AAIController` drives a **Behavior Tree Component** running the tree against the Blackboard, and custom logic is authored as C++ subclasses of `UBTTask_BlueprintBase`/`UBTDecorator_BlueprintBase`/`UBTService_BlueprintBase`, or in pure Blueprint, plugged into the visual tree:

```cpp
UCLASS()
class UBTTask_Attack : public UBTTaskNode
{
    GENERATED_BODY()
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override
    {
        AAIController* AIController = OwnerComp.GetAIOwner();
        // perform attack logic against Blackboard's TargetActor key
        return EBTNodeResult::Succeeded;
    }
};
```

Unity, by contrast, has no first-party Behavior Tree package in the core engine; behavior-tree AI there is provided either through community packages available via the Unity Asset Store or GitHub, or hand-rolled by the studio. This is one of the more consequential first-party capability gaps between the two engines' AI tooling, and — combined with the EQS gap described below — a meaningful factor in why narrative- and AI-heavy single-player titles have historically skewed toward Unreal.

### 13.2 NavMesh & Pathfinding

Pathfinding answers "how do I get from A to B," typically by building a simplified walkable-surface mesh from level geometry and searching it (commonly with A* or a variant) for a route, augmented with per-region cost weighting and manually authored connections for actions like jumps or ladders that the mesh alone can't represent.

Unreal's Navigation Mesh (`RecastNavMesh`), built on the open-source Recast library, is generated from level collision geometry — or, in World Partition worlds, generated and streamed per-cell via World Partitioned Navigation Mesh [61] — with **Nav Modifiers**, **Nav Areas** (cost-weighted regions, such as "avoid lava"), and **Nav Links** (manual connections for jumps or ladders) shaping the mesh; pathfinding queries run via `UNavigationSystemV1`/`FindPathToLocation`. Unity's **AI Navigation package** (`com.unity.ai.navigation`) covers the same ground with directly analogous primitives: `NavMeshSurface` defines a bakeable or runtime-buildable NavMesh region, `NavMeshModifier`/`NavMeshModifierVolume` provide area-cost tagging analogous to Unreal's Nav Areas, and `NavMeshLink` provides manual traversal connections analogous to Nav Links [29][30]; `NavMeshAgent` components handle steering, local avoidance, and path following against the built mesh, with `NavMeshObstacle` for dynamic obstacle carving. Pathfinding and NavMesh capability is roughly at parity between the engines — both are Recast-family or Recast-equivalent systems with area costing and manual links [29][30][61]:

```cpp
// Unreal: pathfinding query
FPathFindingQuery Query;
UNavigationSystemV1::GetCurrent(GetWorld())->FindPathSync(Query);
```

```csharp
// Unity: NavMeshAgent path request
NavMeshAgent agent = GetComponent<NavMeshAgent>();
agent.SetDestination(targetTransform.position);
```

### 13.3 Unreal Environment Query System (EQS)

Pathfinding answers "how do I get there"; a separate, complementary question is "where/what is the best place or target" — which nearby cover point has line of sight to the player and is farthest from other enemies, for example. Unreal's **Environment Query System (EQS)** answers exactly this class of spatial-reasoning question [27][28]. A Query composes **Generators**, which produce candidate points or actors — a grid around a location, actors of a class, points along the navmesh — and **Contexts**, which provide reference frames such as the querier or a target actor, with **Tests** that score each candidate on criteria like distance, line of sight, or navmesh cost, producing a ranked or weighted result the AI can pick from [27][28]:

```cpp
// Running an EQS query and using the best result
FEnvQueryRequest Request(QueryTemplate, AIController->GetPawn());
Request.Execute(EEnvQueryRunMode::SingleResult, this, &AMyAIController::OnQueryFinished);
```

EQS results commonly feed a Blackboard key that a Behavior Tree task then consumes, so the three systems — Behavior Tree, Blackboard, EQS — are purpose-built to compose together into a single decision-making stack.

### 13.4 Unity NavMesh & Behavior Packages

Unity provides no first-party equivalent to EQS, or to the decision-making layer generally — Behavior Trees and spatial reasoning queries alike are left to third-party packages or custom engineering, in contrast to Unreal's complete, first-party AI decision-making stack spanning pathfinding, decision logic, and spatial querying as one coherent, composable system.

---

## 14. Save / Serialization Systems

Save systems persist a subset of runtime game state to disk — and reload it later — in a versioned, forward/backward-compatible format, distinct from an editor's own asset-serialization format even though both typically reuse the engine's core serialization machinery.

Unreal provides a complete, first-party answer: `USaveGame` is the base class for save data, and a project subclasses it with `UPROPERTY()` fields to persist, then calls `UGameplayStatics::CreateSaveGameObject` / `SaveGameToSlot` / `LoadGameFromSlot` (synchronous) or the async equivalents (`AsyncSaveGameToSlot`) to serialize it to a platform-abstracted **Save Slot**, backed by `ISaveGameSystem`, whose concrete implementation varies per platform — a `.sav` file under `Saved/SaveGames/` on desktop, or a platform-specific save API on consoles [39][40]. Because `USaveGame` reuses Unreal's standard `UObject` property serialization, engineers get automatic handling of nested structs, arrays, and soft object references, with `FArchive`-level custom `Serialize()` overrides available for versioning and migration logic when save-data schemas change across game versions:

```cpp
UCLASS()
class UMySaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 CurrentLevel;
    UPROPERTY() TArray<FInventoryItem> Inventory;
};

// Save
UMySaveGame* SaveObj = Cast<UMySaveGame>(UGameplayStatics::CreateSaveGameObject(UMySaveGame::StaticClass()));
SaveObj->CurrentLevel = 3;
UGameplayStatics::SaveGameToSlot(SaveObj, TEXT("Slot0"), 0);

// Load
if (UGameplayStatics::DoesSaveGameExist(TEXT("Slot0"), 0))
{
    UMySaveGame* Loaded = Cast<UMySaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("Slot0"), 0));
}
```

Unity has no first-party high-level save-game framework analogous to `USaveGame`/`ISaveGameSystem`. The engine provides only low-level, general-purpose serialization primitives — `JsonUtility` (fast, but limited to serializable field types, with no polymorphism or dictionary support without custom handling), the `System.Runtime.Serialization`/`BinaryFormatter` family (legacy, and increasingly discouraged across the wider .NET ecosystem for security and versioning reasons, not something specific to Unity), or third-party libraries such as Newtonsoft Json.NET or Odin Serializer — plus `Application.persistentDataPath` as the platform-appropriate writable directory to target with standard `System.IO` file APIs:

```csharp
[System.Serializable]
public class SaveData { public int currentLevel; public List<InventoryItem> inventory; }

void Save(SaveData data)
{
    string json = JsonUtility.ToJson(data);
    File.WriteAllText(Path.Combine(Application.persistentDataPath, "save0.json"), json);
}

SaveData Load() =>
    JsonUtility.FromJson<SaveData>(File.ReadAllText(Path.Combine(Application.persistentDataPath, "save0.json")));
```

Unity's hands-off approach is more flexible — a studio can pick exactly the serialization format and strategy that fits, JSON for readability and moddability, binary for compactness, custom cloud-save integration, and so on — but it means every Unity project re-solves save/load plumbing, save-file versioning, and console platform save-API integration from scratch or via third-party packages, where Unreal's `USaveGame` handles the engine's own complex types and platform save-API abstraction automatically, at the cost of coupling save data to the `UObject` reflection and serialization system. This is one of the more commonly cited first-party gaps in Unity's offering relative to Unreal.

---

## 15. Build, Deployment & Platform Abstraction Layers

Engines abstract platform differences — graphics API, input devices, file I/O, storefront and achievement APIs, executable format — behind a common runtime interface, and provide tooling to compile, package, and deploy a project to each supported target platform from a single codebase. Both engines solve this the same structural way: a generic interface with per-platform concrete implementations, licensed console modules layered on top, and a CLI-invokable build pipeline for continuous integration. The meaningful difference is where the abstraction line sits, and how visible it is to the developer.

Platform abstraction runs deep in Unreal's architecture. The **RHI (Rendering Hardware Interface)** abstracts DirectX 12, Vulkan, Metal, and console graphics APIs behind one rendering-command interface, and the **Platform Abstraction Layer** — `FGenericPlatform*` classes such as `FGenericPlatformFile` and `FGenericPlatformMisc`, with per-platform overrides like `FWindowsPlatformFile` — abstracts file I/O, threading primitives, and OS services. Building for a target platform runs through the three-phase **Build → Cook → Stage/Package** pipeline described in [Section 11.4](#114-cooking--building), driven by **UnrealBuildTool (UBT)** for compilation and **UnrealAutomationTool (UAT)** for the higher-level cook/package/deploy orchestration, invocable via editor UI (`Package Project`) or headless command line for CI (`RunUAT.bat BuildCookRun ...`) — the standard integration point for build farms. Console platform SDKs (PlayStation, Xbox, Switch) integrate as licensed platform extension modules layered on this same abstraction, so game code targeting the generic platform interfaces does not need per-console branches for most systems. Because the engine ships as source, the RHI backends and `FGenericPlatform*` classes are readable and patchable C++ for studios with source access, giving real visibility and customizability at this layer.

Unity's equivalent abstraction is the native runtime itself, which is closed rather than inspectable: switching **Build Settings → Platform** retargets the same C# project against a different platform backend, with graphics API selection per-platform in Player Settings and a choice of scripting backend between **Mono** (JIT, faster iteration and editor builds) and **IL2CPP** (ahead-of-time C++ transpilation, required on iOS, consoles, and WebGL, generally offering better runtime performance and smaller, strippable builds). The `BuildPipeline` API (`BuildPipeline.BuildPlayer`) and Unity Cloud Build, or command-line batch mode (`-batchmode -executeMethod`), provide the CI and build-farm integration point, analogous to UAT's `RunUAT` command-line entry point. Console platform support is delivered via separate, license-gated platform modules obtained through Unity's console-partner program, installed alongside the standard Editor. This closed-runtime model gives less visibility and customizability at the platform-abstraction layer than Unreal's inspectable source, in exchange for not needing to maintain platform-abstraction code at all — the same engine-vs-game separation tradeoff described in [Section 1.4](#14-engine-vs-game-separation) showing up again at the platform layer.

---

## 16. Editor Tooling & Extensibility

### 16.1 Unreal Editor Utility Widgets & Python

Beyond runtime UMG (see [Section 9.1](#91-unreal-umg--slate)), Unreal exposes editor-only extensibility through several layered tools aimed at different audiences. **Editor Utility Widgets (EUW)** are non-modal UMG-built dialog windows that run inside the editor rather than at play-time, scriptable via Blueprint or Python, and used for building bespoke internal tools — batch-rename assets, level-dressing helpers, data-validation panels [45]. The **Scriptable Tools System** generalizes this further into full interactive editor modal tools, such as custom viewport gizmos and manipulators, not just panel-style widgets [45]. **Python scripting** — the editor's embedded Python interpreter, Python 3.11.8 as of recent UE5 releases — exposes most editor subsystems (Asset Registry, Level Editor, Static Mesh Editor operations) as an `unreal` module, callable from Editor Utility Widgets and Blueprints via Python execution nodes, or from standalone scripts for batch and CI automation [44]. **Geometry Scripting** layers procedural mesh generation and editing on top of both EUW and Python for programmatic content tools:

```python
# Unreal Editor Python: batch-tagging assets via the Asset Registry
import unreal

registry = unreal.AssetRegistryHelpers.get_asset_registry()
assets = registry.get_assets_by_path("/Game/Environments", recursive=True)
for asset_data in assets:
    asset = asset_data.get_asset()
    if isinstance(asset, unreal.StaticMesh):
        unreal.EditorAssetLibrary.set_metadata_tag(asset, "Category", "Environment")
```

This stratifies Unreal's editor scripting across three surfaces of increasing power — EUW/Blueprint for designer-accessible tools, Python for automation and batch/CI scripting, full C++ Editor plugins for deep custom asset editors [44][45] — each aimed at a different audience and iteration speed.

### 16.2 Unity Editor Scripting & Custom Inspectors

Unity's editor extensibility collapses this into essentially one surface: pure C#, running in the same language as gameplay code, organized around scripts placed in any `Editor/` folder, which is automatically excluded from player builds. **Custom Inspectors** subclass `Editor`, apply `[CustomEditor(typeof(MyComponent))]`, and override `OnInspectorGUI()` using the classic immediate-mode **IMGUI** approach, or build a `CreateInspectorGUI()` method returning a UI Toolkit `VisualElement` tree for the modern retained-mode approach [46][47]:

```csharp
[CustomEditor(typeof(EnemySpawner))]
public class EnemySpawnerEditor : Editor
{
    public override void OnInspectorGUI()
    {
        DrawDefaultInspector();
        var spawner = (EnemySpawner)target;
        if (GUILayout.Button("Spawn Preview Wave"))
            spawner.SpawnWave();
    }
}
```

**Custom Editor Windows** (`EditorWindow` subclasses) provide standalone tool panels — asset browsers, level-dressing tools, batch operations — and **Property Drawers** (`[CustomPropertyDrawer]`) customize how individual serialized field types render across every inspector that uses them. This single-language surface is usable equally by technical artists and engineers, and lowers the barrier to entry relative to Unreal's C++/Blueprint/Python split — no second language, no Blueprint-vs-C++ decision — but it also means there's no built-in, non-programmer-friendly visual tool-building surface equivalent to Unreal's EUW graphs for designers who don't write C#.

### 16.3 Custom Tools & Plugins

Deeper extensibility follows the same pattern as the rest of each engine's module system. In Unreal, this means **Editor Plugins** (`.uplugin` with an `Editor` module), which can add menu entries, toolbar buttons, asset editors, and detail-panel customizations through the Slate/UMG APIs and the editor's extension points (`FExtender`, `IAssetTypeActions` for custom asset type editors). In Unity, the equivalent is a standard **UPM package** — or plain `Editor/`-scoped scripts — registering `MenuItem` attributes, custom `EditorWindow`s, and `ScriptableWizard`s, distributed either privately via a git URL or local package, or publicly via the Unity Asset Store.

---

## References

All sources are official documentation from Epic Games (dev.epicgames.com) or Unity Technologies (docs.unity3d.com / docs.unity.com / docs-multiplayer.unity3d.com), current as of Unreal Engine 5.8 and Unity 6000.x documentation at time of writing, unless otherwise noted.

1. Epic Games. "Lumen Global Illumination and Reflections in Unreal Engine." https://dev.epicgames.com/documentation/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine
2. Epic Games. "Lumen Technical Details in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine
3. Epic Games. "Physics in Unreal Engine." https://dev.epicgames.com/documentation/unreal-engine/physics-in-unreal-engine
4. Epic Games. "Chaos Destruction in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/chaos-destruction-in-unreal-engine
5. Epic Games. "Chaos Vehicles." https://dev.epicgames.com/documentation/unreal-engine/chaos-vehicles
6. Epic Games. "MetaSounds in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/metasounds-in-unreal-engine
7. Epic Games. "MetaSounds Reference Guide in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/metasounds-reference-guide-in-unreal-engine
8. Epic Games. "Enhanced Input in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine
9. Epic Games. "Gameplay Ability System for Unreal Engine." https://dev.epicgames.com/documentation/unreal-engine/gameplay-ability-system-for-unreal-engine
10. Epic Games. "Understanding the Unreal Engine Gameplay Ability System." https://dev.epicgames.com/documentation/unreal-engine/understanding-the-unreal-engine-gameplay-ability-system
11. Unity Technologies. "Entities overview." Entities package manual. https://docs.unity3d.com/Packages/com.unity.entities@1.0/manual/index.html
12. Unity Technologies. "Entities." Unity Manual. https://docs.unity3d.com/Manual/com.unity.entities.html
13. Unity Technologies. "Netcode for GameObjects." Unity Manual. https://docs.unity3d.com/Manual/com.unity.netcode.gameobjects.html
14. Unity Technologies. "Unity's netcode packages." https://docs.unity.com/en-us/multiplayer/netcode/netcode
15. Unity Technologies. "Input System." Unity Manual. https://docs.unity3d.com/6000.3/Documentation/Manual/com.unity.inputsystem.html
16. Epic Games. "World Partition in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine
17. Epic Games. "World Partition - Data Layers in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition---data-layers-in-unreal-engine
18. Epic Games. "World Partition - Hierarchical Level of Detail in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition---hierarchical-level-of-detail-in-unreal-engine
19. Unity Technologies. "Render pipeline feature comparison." Unity Manual. https://docs.unity3d.com/6000.3/Documentation/Manual/render-pipelines-feature-comparison.html
20. Unity Technologies. "Choose a render pipeline." Unity Manual. https://docs.unity3d.com/Manual/choose-a-render-pipeline.html
21. Unity Technologies. "Introduction to render pipelines." Unity Manual. https://docs.unity3d.com/6000.5/Documentation/Manual/render-pipelines-overview.html
22. Epic Games. "Replication Graph in Unreal Engine." https://dev.epicgames.com/documentation/unreal-engine/replication-graph-in-unreal-engine
23. Epic Games. "Networking and Multiplayer in Unreal Engine." https://dev.epicgames.com/documentation/unreal-engine/networking-and-multiplayer-in-unreal-engine
24. Epic Games. "Replication Graph Overview and Proper Replication Methods." Unreal Engine Tech Blog. https://www.unrealengine.com/tech-blog/replication-graph-overview-and-proper-replication-methods
25. Unity Technologies. "Comparison of UI systems in Unity." Unity Manual. https://docs.unity3d.com/6000.4/Documentation/Manual/UI-system-compare.html
26. Unity Technologies. "Migrate from uGUI to UI Toolkit." Unity Manual. https://docs.unity3d.com/Manual/UIE-Transitioning-From-UGUI.html
27. Epic Games. "Environment Query System in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/environment-query-system-in-unreal-engine
28. Epic Games. "Environment Query System Overview in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/environment-query-system-overview-in-unreal-engine
29. Unity Technologies. "AI Navigation." Unity Manual. https://docs.unity3d.com/6000.1/Documentation/Manual/com.unity.ai.navigation.html
30. Unity Technologies. "AI Navigation." Package manual, v2.0. https://docs.unity3d.com/Packages/com.unity.ai.navigation@2.0/manual/
31. Epic Games. "Asset Registry in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-registry-in-unreal-engine
32. Unity Technologies. "AssetDatabase." Scripting API. https://docs.unity3d.com/6000.0/Documentation/ScriptReference/AssetDatabase.html
33. Unity Technologies. "Addressable Assets System Overview." Addressables package manual. https://docs.unity3d.com/Packages/com.unity.addressables@1.12/manual/AddressableAssetsOverview.html
34. Epic Games. "Unreal Insights in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-insights-in-unreal-engine
35. Epic Games. "Memory Insights in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/memory-insights-in-unreal-engine
36. Unity Technologies. "Garbage collector overview." Unity Manual. https://docs.unity3d.com/6000.0/Documentation/Manual/performance-garbage-collector.html
37. Unity Technologies. "Garbage collection modes." Unity Manual. https://docs.unity3d.com/6000.1/Documentation/Manual/performance-incremental-garbage-collection.html
38. Unity Technologies. "Memory in Unity introduction." Unity Manual. https://docs.unity3d.com/Manual/performance-memory-overview.html
39. Epic Games. "Saving and Loading Your Game in Unreal Engine." https://dev.epicgames.com/documentation/unreal-engine/saving-and-loading-your-game-in-unreal-engine
40. Epic Games. "ISaveGameSystem." API Reference. https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/ISaveGameSystem
41. Epic Games. "State Machines in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/state-machines-in-unreal-engine
42. Unity Technologies. "Introduction to Animator Controllers." Unity Manual. https://docs.unity3d.com/6000.4/Documentation/Manual/class-AnimatorController.html
43. Unity Technologies. "Mecanim Animation system." Unity Manual. https://docs.unity3d.com/6000.2/Documentation/Manual/AnimationOverview.html
44. Epic Games. "Scripting the Unreal Editor Using Python." https://dev.epicgames.com/documentation/en-us/unreal-engine/scripting-the-unreal-editor-using-python
45. Epic Games. "Scriptable Tools System in Unreal Engine." https://dev.epicgames.com/documentation/unreal-engine/scriptable-tools-system-in-unreal-engine
46. Unity Technologies. "Create a custom Inspector." Unity Manual. https://docs.unity3d.com/6000.5/Documentation/Manual/UIE-HowTo-CreateCustomInspector.html
47. Unity Technologies. "Create custom Editors with IMGUI." Unity Manual. https://docs.unity3d.com/Manual/editor-CustomEditors.html
48. Epic Games. "Packaging and Cooking Games in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-and-cooking-games-in-unreal-engine
49. Epic Games. "Cooking Content in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/cooking-content-in-unreal-engine
50. Epic Games. "Build Operations: Cooking, Packaging, Deploying, and Running Projects in Unreal Engine." https://dev.epicgames.com/documentation/unreal-engine/build-operations-cooking-packaging-deploying-and-running-projects-in-unreal-engine
51. Unity Technologies. "Physics integrations in Unity." Unity Manual. https://docs.unity3d.com/6000.3/Documentation/Manual/physics-integrations.html
52. Unity Technologies. "2D Physics." Unity Manual. https://docs.unity3d.com/6000.0/Documentation/Manual/2d-physics/2d-physics.html
53. Epic Games. "Gameplay Framework in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-framework-in-unreal-engine
54. Epic Games. "Actors in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/actors-in-unreal-engine
55. Epic Games. "Components in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/components-in-unreal-engine
56. Epic Games. "Unreal Engine Actor Lifecycle." https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-actor-lifecycle
57. Unity Technologies. "Transforms." Unity Manual. https://docs.unity3d.com/6000.3/Documentation/Manual/class-Transform.html
58. Unity Technologies. "Manage GameObjects in the Hierarchy window." Unity Manual. https://docs.unity3d.com/6000.2/Documentation/Manual/Hierarchy.html
59. Epic Games. "Actor Ticking in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-ticking-in-unreal-engine
60. Epic Games. "Unreal Engine Modules." https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules
61. Epic Games. "World Partitioned Navigation Mesh." https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partitioned-navigation-mesh

