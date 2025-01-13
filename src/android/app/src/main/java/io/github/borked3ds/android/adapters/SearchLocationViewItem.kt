package io.github.borked3ds.android.adapters

import android.annotation.SuppressLint
import android.net.Uri
import android.view.ViewGroup
import androidx.recyclerview.widget.RecyclerView
import io.github.borked3ds.android.databinding.SearchLocationItemBinding

object SearchLocationBindingFactory : BindingFactory {
    override fun createBinding(parent: ViewGroup) =
        SearchLocationItemBinding.inflate(parent.getInflater(), parent, false)
}

open class SearchLocationViewItem(
    var uri: Uri?,
    var onDelete: ((position: Int) -> Unit)? = null
) : SelectableCustomListItem<SearchLocationItemBinding>() {
    private var holder: CustomViewHolder<SearchLocationItemBinding>? = null
    private val adapterPosition get() = holder?.bindingAdapterPosition ?: RecyclerView.NO_POSITION 

    override fun getBindingFactory() = SearchLocationBindingFactory

    @SuppressLint("SetTextI18n")
    override fun bind(holder: CustomViewHolder<SearchLocationItemBinding>, position: Int) {
        this.holder = holder
        val binding = holder.binding

        binding.name.text = uri?.lastPathSegment?.substringAfterLast('/')
        binding.path.text = uri?.encodedPath?.replace("%3A", ":")?.replace("%2F", "/")

        onDelete?.let { onDelete ->
            binding.deleteButton.visibility = ViewGroup.VISIBLE
            binding.deleteButton.setOnClickListener {
                val pos = adapterPosition
                if (pos == RecyclerView.NO_POSITION)
                    return@setOnClickListener
                selectableAdapter?.removeItemAt(pos)

                onDelete.invoke(pos)
            }
        } ?: run {
            binding.deleteButton.visibility = ViewGroup.GONE
        }
    }

    override fun isSameItem(other: CustomListItem<SearchLocationItemBinding>): Boolean =
        getFilterKey() == other.getFilterKey()
}
