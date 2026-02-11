using System.Collections;
using UnityEngine;
using UnityEngine.Networking;

public class FetchImage : MonoBehaviour
{
    [Header("put droplet quad renderer")]
    public Renderer targetRenderer;

    [Header("image url")]
    public string imageUrl = "";

    [Header("StreamingAssets picture name(include jpg/png)")]
    public string streamingFileName = "test.jpg";

    [Header("write it in droplet material")]
    public string textureProperty = "_ExternalTex";

    [Header("Unlit/Texture to show picture")]
    public bool forceShowAsUnlitTexture = true;

    void Start()
    {
        StartCoroutine(DownloadAndApply());
    }

    IEnumerator DownloadAndApply()
    {
        if (targetRenderer == null)
        {
            UnityEngine.Debug.LogError("targetRenderer is not assigned. Drag the droplet Quad Renderer into Target Renderer.");
            yield break;
        }

        string url = imageUrl;
        if (string.IsNullOrWhiteSpace(url))
            url = Application.streamingAssetsPath + "/" + streamingFileName;

        if (!url.StartsWith("http") && !url.StartsWith("file://"))
            url = "file://" + url;

        UnityEngine.Debug.Log("Final request URL: " + url);

        using (UnityWebRequest req = UnityWebRequestTexture.GetTexture(url))
        {
            yield return req.SendWebRequest();

            if (req.result != UnityWebRequest.Result.Success)
            {
                UnityEngine.Debug.LogError("Download failed: " + req.error + "\nURL: " + url);
                yield break;
            }

            Texture2D tex = DownloadHandlerTexture.GetContent(req);
            UnityEngine.Debug.Log("Download succeeded: " + tex.width + "x" + tex.height);

           
            if (forceShowAsUnlitTexture)
            {
                Shader unlit = Shader.Find("Unlit/Texture");
                if (unlit != null)
                {
                    Material m = new Material(unlit);
                    m.mainTexture = tex;
                    targetRenderer.material = m;
                    UnityEngine.Debug.Log("Showing image with Unlit/Texture for verification.");
                    yield break; 
                }
                else
                {
                    UnityEngine.Debug.LogWarning("Unlit/Texture shader not found. Skipping verification.");
                }
            }

       
            Material mat = targetRenderer.material;
            mat.SetFloat("_UseExternal", 1f);
            mat.SetTexture(textureProperty, tex);
            UnityEngine.Debug.Log("Applied texture to droplet material property: " + textureProperty);
        }
    }
}
