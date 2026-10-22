#!/usr/bin/env python3
"""Generates Content/BigCar/Data/graphics.json (graphics options, upscalers, presets).

Kept as a generator so the five presets stay consistent; edit this file and re-run:
    python3 Tools/gen_graphics.py
"""
import json
import os

LEVELS = ["low", "medium", "high", "epic", "cinematic"]


def scal(opt_id, group, default="high"):
    return {
        "id": opt_id,
        "name": f"ui.gfx.{opt_id}",
        "category": "quality",
        "scalability": group,
        "default": default,
        "values": [{"id": lv, "name": f"ui.gfx.level.{lv}", "level": i} for i, lv in enumerate(LEVELS)],
    }


def onoff(opt_id, category, off, on, default="on", restart=False, requires=None):
    o = {
        "id": opt_id,
        "name": f"ui.gfx.{opt_id}",
        "category": category,
        "default": default,
        "values": [dict({"id": "off", "name": "ui.gfx.off"}, **off), dict({"id": "on", "name": "ui.gfx.on"}, **on)],
    }
    if restart:
        o["restart"] = True
    if requires:
        o["requiresCvar"] = requires
    return o


def multi(opt_id, category, values, default):
    return {
        "id": opt_id,
        "name": f"ui.gfx.{opt_id}",
        "category": category,
        "default": default,
        "values": [dict({"id": vid, "name": f"ui.gfx.{opt_id}.{vid}"}, **body) for vid, body in values],
    }


options = [
    scal("shadows", "Shadow"),
    scal("textures", "Texture", "epic"),
    scal("effects", "Effects"),
    scal("post_process", "PostProcess"),
    scal("view_distance", "ViewDistance", "epic"),
    scal("foliage", "Foliage"),
    scal("shading", "Shading"),
    scal("reflection_quality", "Reflection"),
    scal("gi_quality", "GlobalIllumination"),
    scal("aa_quality", "AntiAliasing"),
    multi("gi_method", "lighting", [
        ("off", {"cvars": {"r.DynamicGlobalIlluminationMethod": "0"}}),
        ("ssgi", {"cvars": {"r.DynamicGlobalIlluminationMethod": "2", "r.SSGI.Quality": "3"}}),
        ("lumen", {"cvars": {"r.DynamicGlobalIlluminationMethod": "1"}}),
    ], "lumen"),
    multi("reflection_method", "lighting", [
        ("off", {"cvars": {"r.ReflectionMethod": "0"}}),
        ("ssr", {"cvars": {"r.ReflectionMethod": "2", "r.SSR.Quality": "3"}}),
        ("lumen", {"cvars": {"r.ReflectionMethod": "1"}}),
    ], "lumen"),
    onoff("hardware_rt", "lighting", {"cvars": {"r.Lumen.HardwareRayTracing": "0"}},
          {"cvars": {"r.Lumen.HardwareRayTracing": "1"}}, default="off"),
    onoff("virtual_shadows", "lighting", {"cvars": {"r.Shadow.Virtual.Enable": "0"}},
          {"cvars": {"r.Shadow.Virtual.Enable": "1"}}),
    onoff("contact_shadows", "lighting", {"cvars": {"r.ContactShadows": "0"}}, {"cvars": {"r.ContactShadows": "1"}}),
    onoff("volumetric_fog", "lighting", {"cvars": {"r.VolumetricFog": "0"}}, {"cvars": {"r.VolumetricFog": "1"}}),
    onoff("ambient_occlusion", "post", {"cvars": {"r.AmbientOcclusionLevels": "0"}, "post": {"ambientOcclusion": 0.0}},
          {"cvars": {"r.AmbientOcclusionLevels": "-1"}, "post": {"ambientOcclusion": 0.5}}),
    multi("motion_blur", "post", [
        ("off", {"cvars": {"r.MotionBlurQuality": "0"}, "post": {"motionBlur": 0.0}}),
        ("low", {"cvars": {"r.MotionBlurQuality": "3"}, "post": {"motionBlur": 0.25}}),
        ("high", {"cvars": {"r.MotionBlurQuality": "4"}, "post": {"motionBlur": 0.5}}),
    ], "low"),
    onoff("bloom", "post", {"cvars": {"r.BloomQuality": "0"}, "post": {"bloom": 0.0}},
          {"cvars": {"r.BloomQuality": "5"}, "post": {"bloom": 0.675}}),
    onoff("lens_flares", "post", {"cvars": {"r.LensFlareQuality": "0"}, "post": {"lensFlare": 0.0}},
          {"cvars": {"r.LensFlareQuality": "2"}, "post": {"lensFlare": 1.0}}),
    onoff("depth_of_field", "post", {"cvars": {"r.DepthOfFieldQuality": "0"}}, {"cvars": {"r.DepthOfFieldQuality": "2"}}),
    onoff("chromatic_aberration", "post", {"cvars": {"r.SceneColorFringeQuality": "0"}, "post": {"chromaticAberration": 0.0}},
          {"cvars": {"r.SceneColorFringeQuality": "1"}, "post": {"chromaticAberration": 0.35}}, default="off"),
    onoff("film_grain", "post", {"post": {"filmGrain": 0.0}}, {"post": {"filmGrain": 0.15}}, default="off"),
    onoff("vignette", "post", {"post": {"vignette": 0.0}}, {"post": {"vignette": 0.4}}),
    multi("anisotropy", "quality", [
        ("x2", {"cvars": {"r.MaxAnisotropy": "2"}}),
        ("x4", {"cvars": {"r.MaxAnisotropy": "4"}}),
        ("x8", {"cvars": {"r.MaxAnisotropy": "8"}}),
        ("x16", {"cvars": {"r.MaxAnisotropy": "16"}}),
    ], "x8"),
    multi("sharpening", "post", [
        ("off", {"cvars": {"r.Tonemapper.Sharpen": "0"}}),
        ("low", {"cvars": {"r.Tonemapper.Sharpen": "0.4"}}),
        ("high", {"cvars": {"r.Tonemapper.Sharpen": "0.8"}}),
    ], "low"),
    multi("debris_detail", "game", [
        ("low", {"game": {"maxDebris": 24, "sparkCount": 6, "skidMarks": 0}}),
        ("medium", {"game": {"maxDebris": 64, "sparkCount": 14, "skidMarks": 1}}),
        ("high", {"game": {"maxDebris": 160, "sparkCount": 28, "skidMarks": 1}}),
    ], "medium"),
    multi("deformation_detail", "game", [
        ("low", {"game": {"deformSegments": 8}}),
        ("medium", {"game": {"deformSegments": 14}}),
        ("high", {"game": {"deformSegments": 22}}),
    ], "medium"),
]

def modes_sp(prefix):
    return [
        {"id": "native", "name": "ui.gfx.mode.native", "screenPercentage": 100},
        {"id": "quality", "name": "ui.gfx.mode.quality", "screenPercentage": 66.7},
        {"id": "balanced", "name": "ui.gfx.mode.balanced", "screenPercentage": 58},
        {"id": "performance", "name": "ui.gfx.mode.performance", "screenPercentage": 50},
        {"id": "ultra_performance", "name": "ui.gfx.mode.ultra_performance", "screenPercentage": 33.3},
    ]


def modes_cvar(cvar, values):
    names = ["native", "quality", "balanced", "performance", "ultra_performance"]
    return [{"id": n, "name": f"ui.gfx.mode.{n}", "cvars": {cvar: str(v)}} for n, v in zip(names, values) if v is not None]


fg_amd = {"detectCvar": "r.FidelityFX.FI.Enabled", "on": {"r.FidelityFX.FI.Enabled": "1"},
          "off": {"r.FidelityFX.FI.Enabled": "0"}}

upscalers = [
    {"id": "taa", "name": "ui.gfx.upscaler.taa", "vendor": "epic", "enable": {"r.AntiAliasingMethod": "2"},
     "modes": [{"id": "native", "name": "ui.gfx.mode.custom"}]},
    {"id": "tsr", "name": "ui.gfx.upscaler.tsr", "vendor": "epic", "enable": {"r.AntiAliasingMethod": "4"},
     "modes": modes_sp("tsr")},
    {"id": "fxaa", "name": "ui.gfx.upscaler.fxaa", "vendor": "epic", "enable": {"r.AntiAliasingMethod": "1"},
     "modes": [{"id": "native", "name": "ui.gfx.mode.custom"}]},
    {"id": "dlss", "name": "ui.gfx.upscaler.dlss", "vendor": "nvidia", "detectCvar": "r.NGX.DLSS.Enable",
     "enable": {"r.AntiAliasingMethod": "4", "r.NGX.DLSS.Enable": "1"}, "disable": {"r.NGX.DLSS.Enable": "0"},
     "modes": [
         {"id": "native", "name": "ui.gfx.mode.dlaa", "screenPercentage": 100},
         {"id": "quality", "name": "ui.gfx.mode.quality", "screenPercentage": 66.7},
         {"id": "balanced", "name": "ui.gfx.mode.balanced", "screenPercentage": 58},
         {"id": "performance", "name": "ui.gfx.mode.performance", "screenPercentage": 50},
         {"id": "ultra_performance", "name": "ui.gfx.mode.ultra_performance", "screenPercentage": 33.3},
     ],
     "frameGen": {"detectCvar": "r.Streamline.DLSSG.Enable", "on": {"r.Streamline.DLSSG.Enable": "1"},
                  "off": {"r.Streamline.DLSSG.Enable": "0"}}},
    {"id": "fsr4", "name": "ui.gfx.upscaler.fsr4", "vendor": "amd", "detectCvar": "r.FidelityFX.FSR4.Enabled",
     "enable": {"r.AntiAliasingMethod": "4", "r.FidelityFX.FSR4.Enabled": "1"},
     "disable": {"r.FidelityFX.FSR4.Enabled": "0"},
     "modes": modes_cvar("r.FidelityFX.FSR4.QualityMode", [0, 1, 2, 3, 4]), "frameGen": fg_amd},
    {"id": "fsr", "name": "ui.gfx.upscaler.fsr", "vendor": "amd", "detectCvar": "r.FidelityFX.FSR.Enabled",
     "enable": {"r.AntiAliasingMethod": "4", "r.FidelityFX.FSR.Enabled": "1"},
     "disable": {"r.FidelityFX.FSR.Enabled": "0"},
     "modes": modes_cvar("r.FidelityFX.FSR.QualityMode", [0, 1, 2, 3, 4]), "frameGen": fg_amd},
    {"id": "fsr3", "name": "ui.gfx.upscaler.fsr3", "vendor": "amd", "detectCvar": "r.FidelityFX.FSR3.Enabled",
     "enable": {"r.AntiAliasingMethod": "4", "r.FidelityFX.FSR3.Enabled": "1"},
     "disable": {"r.FidelityFX.FSR3.Enabled": "0"},
     "modes": modes_cvar("r.FidelityFX.FSR3.QualityMode", [0, 1, 2, 3, 4]), "frameGen": fg_amd},
    {"id": "xess", "name": "ui.gfx.upscaler.xess", "vendor": "intel", "detectCvar": "r.XeSS.Enabled",
     "enable": {"r.AntiAliasingMethod": "4", "r.XeSS.Enabled": "1"}, "disable": {"r.XeSS.Enabled": "0"},
     "modes": modes_cvar("r.XeSS.Quality", [None, 3, 2, 1, 0])},
]

def preset(pid, level_index, gi, refl, rt, extras, upscaler_mode):
    lv = LEVELS[level_index]
    values = {o["id"]: lv for o in options if "scalability" in o}
    values.update({"gi_method": gi, "reflection_method": refl, "hardware_rt": rt})
    values.update(extras)
    return {"id": pid, "name": f"ui.gfx.preset.{pid}", "values": values, "upscaler": "tsr", "upscalerMode": upscaler_mode}

presets = [
    preset("low", 0, "off", "ssr", "off", {
        "virtual_shadows": "off", "contact_shadows": "off", "volumetric_fog": "off", "ambient_occlusion": "off",
        "motion_blur": "off", "bloom": "on", "lens_flares": "off", "depth_of_field": "off", "anisotropy": "x2",
        "sharpening": "low", "debris_detail": "low", "deformation_detail": "low"}, "performance"),
    preset("medium", 1, "ssgi", "ssr", "off", {
        "virtual_shadows": "off", "contact_shadows": "off", "volumetric_fog": "on", "ambient_occlusion": "on",
        "motion_blur": "low", "bloom": "on", "lens_flares": "off", "depth_of_field": "off", "anisotropy": "x4",
        "sharpening": "low", "debris_detail": "medium", "deformation_detail": "medium"}, "balanced"),
    preset("high", 2, "lumen", "lumen", "off", {
        "virtual_shadows": "on", "contact_shadows": "on", "volumetric_fog": "on", "ambient_occlusion": "on",
        "motion_blur": "low", "bloom": "on", "lens_flares": "on", "depth_of_field": "on", "anisotropy": "x8",
        "sharpening": "low", "debris_detail": "medium", "deformation_detail": "medium"}, "quality"),
    preset("epic", 3, "lumen", "lumen", "off", {
        "virtual_shadows": "on", "contact_shadows": "on", "volumetric_fog": "on", "ambient_occlusion": "on",
        "motion_blur": "high", "bloom": "on", "lens_flares": "on", "depth_of_field": "on", "anisotropy": "x16",
        "sharpening": "low", "debris_detail": "high", "deformation_detail": "high"}, "quality"),
    preset("cinematic", 4, "lumen", "lumen", "on", {
        "virtual_shadows": "on", "contact_shadows": "on", "volumetric_fog": "on", "ambient_occlusion": "on",
        "motion_blur": "high", "bloom": "on", "lens_flares": "on", "depth_of_field": "on", "anisotropy": "x16",
        "sharpening": "low", "debris_detail": "high", "deformation_detail": "high"}, "native"),
]

doc = {
    "version": 1,
    "options": options,
    "upscalers": upscalers,
    "presets": presets,
    "lowLatency": {"detectCvar": "t.Streamline.Reflex.Enable", "on": {"t.Streamline.Reflex.Enable": "1"},
                   "off": {"t.Streamline.Reflex.Enable": "0"}},
}

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
path = os.path.join(root, "Content", "BigCar", "Data", "graphics.json")
with open(path, "w", encoding="utf-8") as f:
    json.dump(doc, f, ensure_ascii=False, indent=2)
    f.write("\n")
print("wrote", path)
