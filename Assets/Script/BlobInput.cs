using System;
using UnityEngine;
using UnityEngine.InputSystem;

public class BlobInput : MonoBehaviour
{
    [Header("max droplet amount")]
    [Range(1, 64)]
    public int maxBlobs = 64;

    [Header("click area")]
    public float spread = 1.5f;

    [Header("new droplet radius")]
    [Range(0.01f, 2f)]
    public float baseRadius = 0.22f;

    [Header("If click is close to an existing droplet: feed the nearest one (no new droplet)")]
    public float clickFeedDistance = 0.45f;

    [Header("Radius increase per click (added to target radius)")]
    public float clickRadiusIncrement = 0.10f;

    [Header("right click to clear all the droplet")]
    public bool rightClickClear = true;

    private const int Max = 64;

    private int _count = 0;

    // Only stores "initial spawn position" (visual reads it once when created)
    private UnityEngine.Vector3[] _spawnPos = new UnityEngine.Vector3[Max];

    // Target radius (visual reads every frame for SmoothDamp / merge, etc.)
    private float[] _targetRadius = new float[Max];

    private UnityEngine.Vector3[] _impulse = new UnityEngine.Vector3[Max];

    // Clear flag (visual calls ConsumeClearFlag() to reset)
    private bool _clearFlag = false;

    public int Count => _count;

    public UnityEngine.Vector3 GetSpawnPos(int i)
    {
        if (i < 0 || i >= _count) return UnityEngine.Vector3.zero;
        return _spawnPos[i];
    }

    public float GetTargetRadius(int i)
    {
        if (i < 0 || i >= _count) return 0f;
        return _targetRadius[i];
    }

    public UnityEngine.Vector3 ConsumeImpulse(int i)
    {
        if (i < 0 || i >= _count) return UnityEngine.Vector3.zero;
        UnityEngine.Vector3 v = _impulse[i];
        _impulse[i] = UnityEngine.Vector3.zero;
        return v;
    }

    public bool ConsumeClearFlag()
    {
        if (!_clearFlag) return false;
        _clearFlag = false;
        return true;
    }

    private void Update()
    {
        var mouse = Mouse.current;
        if (mouse == null) return;

        if (mouse.leftButton.wasPressedThisFrame)
        {
            UnityEngine.Vector2 sp = mouse.position.ReadValue();
            UnityEngine.Vector3 p = ScreenToPoint(sp);
            ClickAddOrFeed(p);
        }

        if (rightClickClear && mouse.rightButton.wasPressedThisFrame)
        {
            ClearAll();
        }
    }

    // Screen space -> simulation space
    private UnityEngine.Vector3 ScreenToPoint(UnityEngine.Vector2 screenPos)
    {
        float u = Mathf.Clamp01(screenPos.x / Screen.width);
        float v = Mathf.Clamp01(screenPos.y / Screen.height);

        float aspect = Screen.width / (float)Screen.height;

        float x = (u * 2f - 1f) * spread * aspect;
        float y = (v * 2f - 1f) * spread;

        return new UnityEngine.Vector3(x, y, 0f);
    }

    // Add a new droplet, or feed the nearest one if close enough
    private void ClickAddOrFeed(UnityEngine.Vector3 clickPos)
    {
        int limit = Mathf.Min(maxBlobs, Max);

        if (_count > 0)
        {
            int nearest = -1;
            float best = float.PositiveInfinity;

            for (int i = 0; i < _count; i++)
            {
                float d = UnityEngine.Vector3.Distance(_spawnPos[i], clickPos);
                if (d < best)
                {
                    best = d;
                    nearest = i;
                }
            }

            if (nearest >= 0 && best <= clickFeedDistance)
            {
                _targetRadius[nearest] += clickRadiusIncrement;

                UnityEngine.Vector3 dir = clickPos - _spawnPos[nearest];
                if (dir.sqrMagnitude > 1e-6f)
                {
                    _impulse[nearest] += dir.normalized * 0.8f;
                }
                return;
            }
        }

        // Create new droplet
        if (_count >= limit)
        {
            // If full, overwrite the last one
            int idx = limit - 1;
            InitBlob(idx, clickPos, baseRadius);
            return;
        }

        InitBlob(_count, clickPos, baseRadius);
        _count++;
    }

    private void InitBlob(int i, UnityEngine.Vector3 pos, float r)
    {
        _spawnPos[i] = pos;
        _targetRadius[i] = r;
        _impulse[i] = UnityEngine.Vector3.zero;
    }

    private void ClearAll()
    {
        _count = 0;
        _clearFlag = true;
    }
}
