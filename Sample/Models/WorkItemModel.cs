using Microsoft.AspNetCore.Mvc;

namespace Sample.Models;

[BindProperties]
public class WorkItemModel
{
    public long? WorkItemID { get; set; }
    public string? DownloadPath {get;set;}
}
