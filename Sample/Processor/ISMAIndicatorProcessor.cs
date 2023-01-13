namespace Sample;

public interface ISMAIndicatorProcessor {
    public event EventHandler<WorkItem> CompletedWorkItem;
    public void EnqueueWork(WorkItem item);
}