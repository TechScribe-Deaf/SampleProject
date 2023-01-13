using Sample;

var builder = WebApplication.CreateBuilder(args);

// Add SMAIndicatorProcessor for dependency injection.
// For now, this need to be a global object shared among other contexts
// Due to the nature of Vulkan Implementation that could be improved upon
// In the future, as this is only for demostrative purposes.
builder.Services.AddSingleton<ISMAIndicatorProcessor>(new SMAIndicatorProcessor());

// Add services to the container.
builder.Services.AddControllersWithViews();

var app = builder.Build();

// Configure the HTTP request pipeline.
if (!app.Environment.IsDevelopment())
{
    app.UseExceptionHandler("/Home/Error");
    // The default HSTS value is 30 days. You may want to change this for production scenarios, see https://aka.ms/aspnetcore-hsts.
    app.UseHsts();
}

app.UseHttpsRedirection();
app.UseStaticFiles();

app.UseRouting();

app.UseAuthorization();

app.MapControllerRoute(
    name: "default",
    pattern: "{controller=Home}/{action=Index}/{id?}");

app.Urls.Add("http://localhost:8080");
app.Run();
