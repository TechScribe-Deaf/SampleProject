using System.ComponentModel.DataAnnotations;
using Microsoft.AspNetCore.Mvc;

namespace Sample.Models;

[BindProperties]
public class SubmitJobModel
{
    [Required]
    [Display(Name="File")]
    public IFormFile? File {get;set;}
}
