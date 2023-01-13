using System.Diagnostics;
using Microsoft.AspNetCore.Mvc;
using Sample.Models;
using CsvHelper;
using System.Collections.Concurrent;
using System.Security.Cryptography;

namespace Sample.Controllers;

public class DownloadItem {
    public long ID {get;}
    public byte[] Content {get;}

    public DownloadItem(WorkItem item, byte[] content)
    {
        if (content is null)
            throw new NullReferenceException($"{nameof(content)} cannot be null.");
        ID = item.WorkItemIndex;
        Content = content;
    }
}

public class APIController : Controller
{
    private readonly ILogger<HomeController> _logger;
    private readonly ISMAIndicatorProcessor _processor;
    // This is not a recommended approach to storing download files, but for demonstrative purpose, this will do to simplify code.
    protected static ConcurrentDictionary<long, DownloadItem> DownloadCache {get;} = new ConcurrentDictionary<long, DownloadItem>();
    public APIController(ILogger<HomeController> logger, ISMAIndicatorProcessor processor)
    {
        _logger = logger;
        _processor = processor;
        _processor.CompletedWorkItem += (object? sender, WorkItem item) => {

            using var memStream = new MemoryStream();
            using var streamWriter = new StreamWriter(memStream);
            using var writer = new CsvWriter(streamWriter, System.Globalization.CultureInfo.GetCultureInfo("en-US"));

            var smaOutput = item.ComputedOutput[0].Select(I => new {SMA = I}).ToArray();
            writer.WriteHeader(smaOutput.First().GetType());
            writer.WriteRecords(smaOutput);
            writer.Flush();
            streamWriter.Flush();
            memStream.Flush();
            var downloadItem = new DownloadItem(item, memStream.GetBuffer());
            DownloadCache.TryAdd(item.WorkItemIndex, downloadItem);
        };
    }

    [HttpPost]
    public async Task<IActionResult> SubmitJob(SubmitJobModel model)
    {
        if (!ModelState.IsValid) return StatusCode(400);
        if (model.File is null || model.File.Length <= 0)
        {
            return StatusCode(400);
        }
        using var memStream = new MemoryStream();
        await model.File.CopyToAsync(memStream);
        memStream.Seek(0, SeekOrigin.Begin);
        using var streamReader = new StreamReader(memStream);
        using var reader = new CsvReader(streamReader, System.Globalization.CultureInfo.GetCultureInfo("en-US"));
        var pricePoints = reader.GetRecords<PricePoint>();
        var newWorkItem = new WorkItem(pricePoints.ToArray());
        streamReader.Close();
        _processor.EnqueueWork(newWorkItem);
        return RedirectToAction("PendingJob","Home", new WorkItemModel { WorkItemID = newWorkItem.WorkItemIndex});
    }

    [HttpGet]
    public IActionResult CheckJob(WorkItemModel model)
    {
        if (!ModelState.IsValid || model is null) return NotFound();
        if (!model.WorkItemID.HasValue)
            return NotFound();
        if (!DownloadCache.TryGetValue((model?.WorkItemID.Value ?? 0), out var downloadItem))
        {
            return NotFound();
        }
        model.DownloadPath = $"/API/Download?id={downloadItem.ID}";
        return Json(model);
    }

    [HttpGet]
    public IActionResult Download(long id)
    {
        if (!ModelState.IsValid) return NotFound();
        DownloadCache.TryGetValue(id, out var downloadItem);
        if (downloadItem is null) return NotFound();
        return File(downloadItem.Content, "text/csv", "Output.csv");
    }

    [ResponseCache(Duration = 0, Location = ResponseCacheLocation.None, NoStore = true)]
    public IActionResult Error()
    {
        return View(new ErrorViewModel { RequestId = Activity.Current?.Id ?? HttpContext.TraceIdentifier });
    }
}
