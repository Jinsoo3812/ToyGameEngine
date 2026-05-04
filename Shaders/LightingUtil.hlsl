//***************************************************************************************
// LightingUtil.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Contains API for shader lighting.
//***************************************************************************************

#define MaxLights 16

struct Light
{
    // HLSL packing 규칙에 따른 16byte 정렬
    float3 Strength; // 빛의 세기
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
};

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Shininess;
};

// Point Light & Spot Light의 선형 감쇄
float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    return saturate((falloffEnd-d) / (falloffEnd - falloffStart));
}

// Fresnel 반사율(Schlick 근사) 계산 (반사되는 빛의 비율)
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec)); // 입사각

    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0); // R0 + (1-R0)*(1-cos(입사각))^5

    return reflectPercent;
}

// Blinn-Phong 조명 모델 계산 (카메라에 도달한 반사광의 양)
float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.Shininess * 256.0f; // 거칠기
    float3 halfVec = normalize(toEye + lightVec); // Half vector (빛과 시선의 중간 벡터)

    float roughnessFactor = (m + 8.0f)*pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f; // 시선 방향으로 반사시키는 미세면의 비율 (m이 클수록 매끄러워 정반사 하이라이트가 좁다)
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec); // 입사광에 대한 반사광의 비율 (빛이 표면에 수직으로 입사할수록 반사광이 많아짐)

    float3 specAlbedo = fresnelFactor*roughnessFactor; // 반사광의 양 (정반사율)
    specAlbedo = specAlbedo / (specAlbedo + 1.0f);

    return (mat.DiffuseAlbedo.rgb + specAlbedo) * lightStrength; // 직접광에 대한 최종 조명 = (난반사율 + 정반사율) * 빛의 세기
}

// 직접광 (ex. 태양) 계산
float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 toEye)
{
    // Light vector = 빛을 바라보는 방향
    float3 lightVec = -L.Direction;

    // 람베르트 코사인 법칙에 의한 감쇄
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    // 반사에 대한 조명방정식 (Blinn-Phong 모델) 계산
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

// 점광 (ex. 전구) 계산
float3 ComputePointLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // Light vector = 빛을 바라보는 방향 (표면에서 광원으로)
    float3 lightVec = L.Position - pos;

    // 광원과의 거리
    float d = length(lightVec);

    // 너무 멀면 0
    if(d > L.FalloffEnd)
        return 0.0f;

    // 단위 벡터 L
    lightVec /= d;

    // 람베르트 코사인 법칙에 의한 감쇄
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    // 선형 감쇄
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    // 반사에 대한 조명방정식 (Blinn-Phong 모델) 계산
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

// SpltLight (ex. 손전등) 계산
float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // Light vector = 빛을 바라보는 방향 (표면에서 광원으로)
    float3 lightVec = L.Position - pos;

    // 광원과의 거리
    float d = length(lightVec);

    // 너무 멀면 0
    if(d > L.FalloffEnd)
        return 0.0f;

    // 단위 벡터 L
    lightVec /= d;

    // 람베르트 코사인 법칙에 의한 감쇄
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    // 선형 감쇄
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    // max(dot(-L, d), 0)^SpotPower에 의한 감쇄 (빛이 스포트 라이트의 중심에서 멀어질수록 감쇄)
    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.0f), L.SpotPower);
    lightStrength *= spotFactor;

    // 반사에 대한 조명방정식 (Blinn-Phong 모델) 계산
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

float4 ComputeLighting(Light gLights[MaxLights], Material mat,
                       float3 pos, float3 normal, float3 toEye,
                       float3 shadowFactor)
{
    float3 result = 0.0f;

    int i = 0;

#if (NUM_DIR_LIGHTS > 0)
    for(i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        result += shadowFactor[i] * ComputeDirectionalLight(gLights[i], mat, normal, toEye);
    }
#endif

#if (NUM_POINT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS+NUM_POINT_LIGHTS; ++i)
    {
        result += ComputePointLight(gLights[i], mat, pos, normal, toEye);
    }
#endif

#if (NUM_SPOT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        result += ComputeSpotLight(gLights[i], mat, pos, normal, toEye);
    }
#endif 

    return float4(result, 0.0f);
}


