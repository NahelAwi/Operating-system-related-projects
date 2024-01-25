#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sched.h>

asmlinkage long sys_hello(void){
	printk("Hello, World!\n");
	return 0;
}

asmlinkage long sys_set_weight(int weight){
	if(weight < 0){
    	return -EINVAL;
  	}
  	current->weight = weight;
  	return 0;
}

asmlinkage long sys_get_weight(void){
	return current->weight;
}

int traverse_children_sum_weight(struct task_struct *root_task){
  struct task_struct *task;
  struct list_head *list;
  int sum = 0;
  bool flag = 0;

  list_for_each(list, &root_task->children){
    task = list_entry(list, struct task_struct, sibling);
    sum += traverse_children_sum_weight(task);
    flag = 1;
  }
  if(flag)
    return sum;
  else
    return sum + root_task->weight;
}

asmlinkage long sys_get_leaf_children_sum(void){
  int sum_weight = 0;
  bool parent = 0;
  struct task_struct *task;
  struct list_head *list;

  list_for_each(list, &current->children){
    task = list_entry(list, struct task_struct, sibling);
    sum_weight += traverse_children_sum_weight(task);
    parent = 1;
  }
  if(parent)
    return sum_weight;
  else
    return -ECHILD;
}

asmlinkage long sys_get_heaviest_ancestor(void) {
  int max_weight = current->weight;
  pid_t max_weight_pid = current->pid;
  struct task_struct *task, *prev;
  task = current;
  do {
    if (task->weight > max_weight) {
      max_weight = task->weight;
      max_weight_pid = task->pid;
    }
    prev = task;
    task = task->parent;
  } while (prev->pid != 0);

  return max_weight_pid;
}
