using System;
using System.Collections.Concurrent;
using Sample;

namespace Sample {
    public class SMAIndicatorProcessor : ISMAIndicatorProcessor {
        /// <summary>
        /// Thread for processing a batch of price points for Vulkan Compute.
        /// When working with P/Invoke, it is sometime better to create Thread manually than building Async code.
        /// Plus for this demonstration, Vulkan Compute is currently designed for single thread use, so
        /// It's better to have vulkan computation run on separate thread.
        /// </summary>
        private Thread? WorkerThread {get;set;}
        public ConcurrentQueue<WorkItem> WorkItemQueue {get;set;} = new ConcurrentQueue<WorkItem>();
        public event EventHandler<WorkItem>? CompletedWorkItem;

        public void EnqueueWork(WorkItem workitem)
        {
            WorkItemQueue.Enqueue(workitem);
            Start();
        }

        protected virtual void Start()
        {
            if (WorkerThread is null)
                WorkerThread = new Thread(WorkerThreadAction);
            else if (WorkerThread.ThreadState == ThreadState.Running)
                return;
            else
            {
                WorkerThread.Join(); // Terminates the thread and then allow it to be garbage collected
                WorkerThread = new Thread(WorkerThreadAction); // Assign new reference to create new thread going forward
            }
            WorkerThread.Start();
        }

        protected virtual void WorkerThreadAction(object? obj) {
            while (!WorkItemQueue.IsEmpty)
            {
                if (!WorkItemQueue.TryDequeue(out var item))
                {
                    continue;
                }

                var result = LibComputeSample.Compute(item);
                if (result is null)
                {
                    CompletedWorkItem?.Invoke(this, item);
                    return;
                }
                item.ComputedOutput = new float[1][];
                item.ComputedOutput[0] = result;
                CompletedWorkItem?.Invoke(this, item);
            }
        }
    }
}