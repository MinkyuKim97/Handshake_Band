using System;
using UnityEngine;

public class BlobVisual : MonoBehaviour
{
    [Header("数据源：把 BlobInput 拖到这里")]
    public BlobInput source;

    [Header("把挂了水滴材质的 Quad 的 Renderer 拖进来（推荐）")]
    public Renderer targetRenderer;

    [Header("不使用 Renderer 的话，就把材质直接拖这里")]
    public Material targetMaterial;

    [Header("半径平滑时间（越小越快）")]
    [Range(0.01f, 1f)]
    public float radiusSmoothTime = 0.12f;

    [Header("自动吸引范围")]
    public float attractRange = 1.1f;

    [Header("自动吸引强度")]
    public float attractStrength = 6.0f;

    [Header("位置阻尼（越大越稳）")]
    public float positionDamping = 5.0f;

    [Header("开始拉近的距离（进入更强拉扯）")]
    public float mergeStartDistance = 0.35f;

    [Header("开始合并的距离（小于这个就进入合并流程）")]
    public float mergeBeginDistance = 0.18f;

    [Header("合并插值速度（位置/半径趋近速度）")]
    public float mergeSpeed = 2.8f;

    [Header("合并时：源水滴缩小速度")]
    public float mergeShrinkSpeed = 4.5f;

    [Header("源水滴半径小于这个就删掉")]
    public float mergeKillRadius = 0.02f;

    [Header("合并时额外拉力增强")]
    public float mergePullBoost = 10.0f;

    [Header("流动感：位置抖动幅度")]
    public float flowPosAmplitude = 0.06f;

    [Header("流动感：位置抖动速度")]
    public float flowPosSpeed = 2.2f;

    [Header("流动感：半径呼吸幅度（0.1 = 10%）")]
    [Range(0f, 0.5f)]
    public float flowRadiusAmplitude = 0.10f;

    [Header("流动感：半径呼吸速度")]
    public float flowRadiusSpeed = 2.6f;

    private const int Max = 64;

    private static readonly int UseExternalID = Shader.PropertyToID("_UseExternal");
    private static readonly int BlobCountID = Shader.PropertyToID("_BlobCount");
    private static readonly int BlobsID = Shader.PropertyToID("_Blobs");

    private int _count = 0;

    // 仅视觉层：位置与速度
    private UnityEngine.Vector3[] _pos = new UnityEngine.Vector3[Max];
    private UnityEngine.Vector3[] _vel = new UnityEngine.Vector3[Max];

    // 半径：当前/目标/平滑速度
    private float[] _radius = new float[Max];
    private float[] _targetRadius = new float[Max];
    private float[] _radiusVel = new float[Max];

    // 合并状态：src -> dst
    private bool[] _isMerging = new bool[Max];
    private int[] _mergeDst = new int[Max];
    private float[] _mergeFinalRadius = new float[Max];

    // 流动噪声种子
    private float[] _seedA = new float[Max];
    private float[] _seedB = new float[Max];
    private float[] _seedC = new float[Max];

    // 上传到 shader 的数据：xyz = 位置，w = 半径
    private UnityEngine.Vector4[] _upload = new UnityEngine.Vector4[Max];

    private bool _ok = false;

    private void Start()
    {
        // 如果提供了 Renderer，则从 Renderer 取材质（实例材质）
        if (targetRenderer != null)
        {
            targetMaterial = targetRenderer.material;
        }

        // 材质必须存在
        if (targetMaterial == null)
        {
            UnityEngine.Debug.LogError("BlobVisual: targetRenderer 或 targetMaterial 没有赋值。");
            enabled = false;
            return;
        }

        // 告诉 shader 使用外部传入的水滴数组
        targetMaterial.SetFloat(UseExternalID, 1f);
        _ok = true;
    }

    private void Update()
    {
        if (!_ok) return;
        if (source == null) return;

        // 处理清空
        if (source.ConsumeClearFlag())
        {
            _count = 0;
            targetMaterial.SetInt(BlobCountID, 0);
            return;
        }

        int srcCount = Mathf.Clamp(source.Count, 0, Max);

        // 新增：初始化新水滴的视觉状态
        if (srcCount > _count)
        {
            for (int i = _count; i < srcCount; i++)
            {
                UnityEngine.Vector3 spawn = source.GetSpawnPos(i);
                float tr = source.GetTargetRadius(i);

                _pos[i] = spawn;
                _vel[i] = UnityEngine.Vector3.zero;

                _radius[i] = tr;
                _targetRadius[i] = tr;
                _radiusVel[i] = 0f;

                _isMerging[i] = false;
                _mergeDst[i] = -1;
                _mergeFinalRadius[i] = 0f;

                _seedA[i] = UnityEngine.Random.Range(0f, 1000f);
                _seedB[i] = UnityEngine.Random.Range(0f, 1000f);
                _seedC[i] = UnityEngine.Random.Range(0f, 1000f);
            }

            _count = srcCount;
        }
        else if (srcCount < _count)
        {
            // 数据源减少：直接缩短 count（这里不做复杂搬运，保持简单）
            _count = srcCount;
        }

        // 同步目标半径 + 吃掉输入冲量（例如点击给一个 impulse）
        for (int i = 0; i < _count; i++)
        {
            _targetRadius[i] = source.GetTargetRadius(i);

            UnityEngine.Vector3 imp = source.ConsumeImpulse(i);
            if (imp.sqrMagnitude > 1e-6f)
            {
                _vel[i] += imp;
            }
        }

        Simulate(Time.deltaTime);
        UploadToShader();
    }

    private void Simulate(float dt)
    {
        if (_count <= 0) return;

        // 半径平滑：跟随数据源的目标半径
        for (int i = 0; i < _count; i++)
        {
            _radius[i] = Mathf.SmoothDamp(_radius[i], _targetRadius[i], ref _radiusVel[i], radiusSmoothTime, Mathf.Infinity, dt);
        }

        // 吸引与触发合并（仅对未合并的水滴对进行判断）
        for (int i = 0; i < _count; i++)
        {
            if (_isMerging[i]) continue;

            for (int j = i + 1; j < _count; j++)
            {
                if (_isMerging[j]) continue;

                UnityEngine.Vector3 d = _pos[j] - _pos[i];
                float dist = d.magnitude;
                if (dist < 1e-5f) continue;

                if (dist <= attractRange)
                {
                    UnityEngine.Vector3 dir = d / dist;
                    float t = 1f - (dist / attractRange);
                    float force = attractStrength * t;

                    // 越接近 mergeStartDistance，额外增强拉力
                    if (dist <= mergeStartDistance)
                    {
                        force += mergePullBoost * (1f - dist / mergeStartDistance);
                    }

                    _vel[i] += dir * force * dt;
                    _vel[j] -= dir * force * dt;

                    // 真正开始合并
                    if (dist <= mergeBeginDistance)
                    {
                        BeginMerge(i, j);
                    }
                }
            }
        }

        // 执行合并：src 向 dst 靠拢，src 缩小，dst 增大到最终半径
        for (int i = 0; i < _count; i++)
        {
            if (!_isMerging[i]) continue;

            int dst = _mergeDst[i];
            if (dst < 0 || dst >= _count || dst == i)
            {
                _isMerging[i] = false;
                continue;
            }

            float lerpPos = 1f - Mathf.Exp(-mergeSpeed * dt);
            _pos[i] = UnityEngine.Vector3.Lerp(_pos[i], _pos[dst], lerpPos);

            _targetRadius[i] = Mathf.Max(0f, _targetRadius[i] - mergeShrinkSpeed * dt);

            float lerpRad = 1f - Mathf.Exp(-mergeSpeed * dt);
            _targetRadius[dst] = Mathf.Lerp(_targetRadius[dst], _mergeFinalRadius[i], lerpRad);

            // 源水滴缩到阈值就移除
            if (_radius[i] <= mergeKillRadius || _targetRadius[i] <= mergeKillRadius)
            {
                RemoveVisual(i);
                i--;
            }
        }

        // 位置积分（带阻尼）
        float damp = Mathf.Clamp01(1f - positionDamping * dt);
        for (int i = 0; i < _count; i++)
        {
            _vel[i] *= damp;
            _pos[i] += _vel[i] * dt;
        }
    }

    private void BeginMerge(int a, int b)
    {
        if (_isMerging[a] || _isMerging[b]) return;

        // 选择更大的作为 dst，更小的作为 src
        int dst = (_targetRadius[a] >= _targetRadius[b]) ? a : b;
        int src = (dst == a) ? b : a;

        // 简单“体积守恒”近似：体积 ~ r^3
        float rDst = Mathf.Max(0.0001f, _targetRadius[dst]);
        float rSrc = Mathf.Max(0.0001f, _targetRadius[src]);

        float vDst = rDst * rDst * rDst;
        float vSrc = rSrc * rSrc * rSrc;
        float rFinal = Mathf.Pow(vDst + vSrc, 1f / 3f);

        _isMerging[src] = true;
        _mergeDst[src] = dst;
        _mergeFinalRadius[src] = rFinal;

        // 给 dst 一点轻微速度，增强“吸收”动感
        UnityEngine.Vector3 dir = (_pos[src] - _pos[dst]);
        if (dir.sqrMagnitude > 1e-6f)
        {
            _vel[dst] += dir.normalized * 0.6f;
        }
    }

    private void RemoveVisual(int index)
    {
        int last = _count - 1;
        if (index < 0 || index > last) return;

        // 用最后一个覆盖当前，避免移动大量数组（O(1) 删除）
        if (index != last)
        {
            _pos[index] = _pos[last];
            _vel[index] = _vel[last];

            _radius[index] = _radius[last];
            _targetRadius[index] = _targetRadius[last];
            _radiusVel[index] = _radiusVel[last];

            _isMerging[index] = _isMerging[last];
            _mergeDst[index] = _mergeDst[last];
            _mergeFinalRadius[index] = _mergeFinalRadius[last];

            _seedA[index] = _seedA[last];
            _seedB[index] = _seedB[last];
            _seedC[index] = _seedC[last];

            // 修正：如果有人正在合并到 last，dst 指针要改成 index
            for (int i = 0; i < _count; i++)
            {
                if (_isMerging[i] && _mergeDst[i] == last)
                {
                    _mergeDst[i] = index;
                }
            }
        }

        _count--;
    }

    private void UploadToShader()
    {
        int count = Mathf.Clamp(_count, 0, Max);
        targetMaterial.SetInt(BlobCountID, count);

        float t = Time.time;

        for (int i = 0; i < count; i++)
        {
            // 位置微抖动（增强“流动”感）
            float ox = Mathf.Sin(t * flowPosSpeed + _seedA[i]);
            float oy = Mathf.Sin(t * (flowPosSpeed * 1.17f) + _seedB[i]);
            UnityEngine.Vector3 flowOffset = new UnityEngine.Vector3(ox, oy, 0f) * flowPosAmplitude;

            // 半径呼吸（增强“水感”）
            float breath = 1f + flowRadiusAmplitude * Mathf.Sin(t * flowRadiusSpeed + _seedC[i]);
            float r = Mathf.Max(0.0001f, _radius[i] * breath);

            UnityEngine.Vector3 p = _pos[i] + flowOffset;
            _upload[i] = new UnityEngine.Vector4(p.x, p.y, p.z, r);
        }

        targetMaterial.SetVectorArray(BlobsID, _upload);
    }
}
